# Source Make recipes are tracked; the populated build_make workspace is generated.
function Initialize-CourseBuild {
    param([Parameter(Mandatory=$true)][string]$CourseRoot)
    $rulesRoot = (Resolve-Path -LiteralPath (Join-Path $CourseRoot "build_rules")).Path
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $CourseRoot "build_make"))
    New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $rulesRoot -File -Recurse) {
        $relative = $file.FullName.Substring($rulesRoot.Length + 1)
        $destination = [IO.Path]::GetFullPath((Join-Path $buildRoot $relative))
        if (-not $destination.StartsWith($buildRoot + [IO.Path]::DirectorySeparatorChar,
                                          [StringComparison]::OrdinalIgnoreCase)) {
            throw "Recipe destination escaped the build workspace."
        }
        New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destination)) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    }
}
