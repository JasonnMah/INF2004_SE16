"""Durable 4 KiB batch assembly. A receipt never promises an unfinished CSV.

The HTTP owner serializes calls with its existing storage lock. Files, rather
than a process-local counter, establish progress after a receiver restart.
"""
from pathlib import Path
import hashlib
import json
import os
import re

BATCH_BYTES = 4096
MAX_LOG_BYTES = 1024 * 1024
KEY = re.compile(r'batch-([0-9a-f]{64})-([0-9a-f]{8})')


class BatchError(ValueError):
    def __init__(self, message, status=400):
        super().__init__(message)
        self.status = status


def _atomic_write(path, data):
    temporary = path.with_suffix(path.suffix + '.tmp')
    try:
        with temporary.open('wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        # Windows does not expose directory fsync through this interface.
        if os.name == 'posix':
            descriptor = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
            try:
                os.fsync(descriptor)
            finally:
                os.close(descriptor)
    finally:
        temporary.unlink(missing_ok=True)


def store_batch(directory, key, total, body):
    """Return the receipt result and, only when complete, the whole-log path.

    X-Log-ID = batch-<whole-log SHA256>-<8 lowercase hex offset digits>.
    X-Log-Total = original binary length. The HTTP body is compressed bytes
    only; the batch identity is outside it. Conflicts never replace old data.
    """
    match = KEY.fullmatch(key)
    if not match or type(total) is not int or not 0 <= total <= MAX_LOG_BYTES:
        raise BatchError('invalid batch identity or total length')
    digest, offset_text = match.groups()
    offset = int(offset_text, 16)
    if offset % BATCH_BYTES or offset > total or (total and offset == total):
        raise BatchError('invalid batch offset')
    expected = min(BATCH_BYTES, total - offset)
    if len(body) != expected:
        raise BatchError('batch body does not match its position and total')

    root = Path(directory)
    folder = root / '.batches' / digest
    folder.mkdir(parents=True, exist_ok=True)
    manifest = folder / 'manifest.json'
    description = {'version': 1, 'sha256': digest, 'length': total, 'batch_bytes': BATCH_BYTES}
    if manifest.exists():
        try:
            previous = json.loads(manifest.read_text(encoding='utf-8'))
        except (ValueError, UnicodeError) as error:
            raise BatchError('damaged saved batch manifest', 409) from error
        if previous != description:
            raise BatchError('log identity reused with different metadata', 409)
    else:
        _atomic_write(manifest, (json.dumps(description, sort_keys=True) + '\n').encode())

    part = folder / (offset_text + '.bin')
    duplicate = part.exists()
    if duplicate:
        if part.read_bytes() != body:
            raise BatchError('batch identity reused with different bytes', 409)
    else:
        _atomic_write(part, body)

    positions = range(0, total, BATCH_BYTES) if total else (0,)
    paths = [folder / f'{position:08x}.bin' for position in positions]
    if not all(path.exists() for path in paths):
        return {'result': 'duplicate' if duplicate else 'stored', 'path': None,
                'offset': offset, 'total': total}

    # The largest supported log is 1 MiB on the PC. Validate every part and
    # the full content hash before publishing a .bin or exposing any CSV.
    pieces = []
    for position, path in zip(positions, paths):
        piece = path.read_bytes()
        if len(piece) != min(BATCH_BYTES, total - position):
            raise BatchError('damaged stored batch length', 422)
        pieces.append(piece)
    complete = b''.join(pieces)
    if hashlib.sha256(complete).hexdigest() != digest:
        raise BatchError('complete log SHA256 mismatch; fragments retained', 422)
    whole_key = 'log-' + digest
    final = root / (hashlib.sha256(whole_key.encode('ascii')).hexdigest() + '.bin')
    if final.exists():
        if final.read_bytes() != complete:
            raise BatchError('existing completed log conflicts', 409)
    else:
        _atomic_write(final, complete)
    return {'result': 'duplicate' if duplicate else 'stored', 'path': final,
            'offset': offset, 'total': total}
