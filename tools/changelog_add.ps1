param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Added", "Changed", "Deprecated", "Removed", "Fixed", "Security")]
    [string]$Type,

    [Parameter(Mandatory = $true)]
    [string]$Message,

    [string]$Path = "CHANGELOG.md"
)

if (-not (Test-Path -LiteralPath $Path)) {
    throw "Changelog file not found: $Path"
}

$raw = Get-Content -LiteralPath $Path -Raw
$lineArray = $raw -split "`r?`n"
$lines = [System.Collections.Generic.List[string]]::new()
$lines.AddRange($lineArray)

$unreleasedIndex = -1
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -eq "## [Unreleased]") {
        $unreleasedIndex = $i
        break
    }
}

if ($unreleasedIndex -lt 0) {
    throw "Could not find '## [Unreleased]' section in $Path"
}

$unreleasedEnd = $lines.Count
for ($i = $unreleasedIndex + 1; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match '^## \[') {
        $unreleasedEnd = $i
        break
    }
}

$categoryHeader = "### $Type"
$categoryIndex = -1
for ($i = $unreleasedIndex + 1; $i -lt $unreleasedEnd; $i++) {
    if ($lines[$i] -eq $categoryHeader) {
        $categoryIndex = $i
        break
    }
}

$entry = "- $Message"

if ($categoryIndex -ge 0) {
    $insertIndex = $categoryIndex + 1
    $lines.Insert($insertIndex, $entry)
}
else {
    $insertBlock = [System.Collections.Generic.List[string]]::new()

    if ($unreleasedEnd -gt 0 -and $lines[$unreleasedEnd - 1] -ne "") {
        $insertBlock.Add("")
    }

    $insertBlock.Add($categoryHeader)
    $insertBlock.Add($entry)
    $insertBlock.Add("")

    $lines.InsertRange($unreleasedEnd, $insertBlock)
}

$final = [string]::Join("`r`n", $lines)
Set-Content -LiteralPath $Path -Value $final -NoNewline

Write-Host "Added changelog entry to '$Path' under '$Type'."
