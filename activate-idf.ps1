$env:IDF_TOOLS_PATH = "C:\Users\Emilio\.espressif"
$env:IDF_PATH       = "D:\BIOORIGEN\DESHIDRATADORES\SISTEMA PANTALLA TACTIL\Firmware Control Tactil\esp-idf-v6.0.1"
$py = "C:\Users\Emilio\.espressif\python_env\idf6.0_py3.14_env\Scripts\python.exe"
$exports = & $py "$env:IDF_PATH\tools\activate.py" --export
if ($LASTEXITCODE -ne 0) { Write-Error "activate.py failed"; exit 1 }
. ([scriptblock]::Create($exports))
