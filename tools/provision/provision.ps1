# provision.ps1 - Pre-carga de identidad de fabrica para el FA-10T.
#
# Cada equipo se flashea con su dev_id + token unicos en la particion NVS
# "factory" (ver partitions.csv). Esa region NO se borra al resetear config o
# WiFi, asi que la identidad sobrevive a todo salvo un re-flasheo total.
#
# Flujo de produccion (por equipo, despues de flashear el firmware comun):
#   1. activar el entorno IDF:   . .\activate-idf.ps1   (desde la raiz del repo)
#   2. correr este script con el ID/token/puerto del equipo:
#
#        .\tools\provision\provision.ps1 -DevId "FA10T-0001" `
#            -Token "xxxxxxxx" -Port COM3
#
# Genera un CSV temporal + un .bin NVS de 0x4000 y lo escribe en la particion
# "factory" con parttool.py (lee la tabla del equipo, sin offsets a mano).
#
# Requisitos: entorno ESP-IDF activado (IDF_PATH seteado).

param(
    [Parameter(Mandatory = $true)] [string] $DevId,
    [Parameter(Mandatory = $true)] [string] $Token,
    [Parameter(Mandatory = $true)] [string] $Port,
    [string] $CloudUrl = "https://bioorigen-web.vercel.app",
    [string] $PartSize = "0x4000"
)

if (-not $env:IDF_PATH) {
    Write-Error "IDF_PATH no esta seteado. Activa el entorno: . .\activate-idf.ps1"
    exit 1
}

$here    = Split-Path -Parent $MyInvocation.MyCommand.Path
$csv     = Join-Path $here "factory_$DevId.csv"
$bin     = Join-Path $here "factory_$DevId.bin"
$nvsGen  = Join-Path $env:IDF_PATH "components\nvs_flash\nvs_partition_generator\nvs_partition_gen.py"
$partTool= Join-Path $env:IDF_PATH "components\partition_table\parttool.py"

# 1. CSV de identidad (gitignored: contiene el token).
@"
key,type,encoding,value
fa10t_cloud,namespace,,
dev_id,data,string,$DevId
token,data,string,$Token
cloud_url,data,string,$CloudUrl
"@ | Out-File -FilePath $csv -Encoding ascii

# 2. Generar la imagen NVS.
Write-Host "Generando imagen NVS para $DevId..." -ForegroundColor Cyan
python $nvsGen generate $csv $bin $PartSize
if ($LASTEXITCODE -ne 0) { Write-Error "nvs_partition_gen fallo"; exit 1 }

# 3. Escribir en la particion 'factory' del equipo conectado.
Write-Host "Escribiendo particion 'factory' en $Port..." -ForegroundColor Cyan
python $partTool --port $Port write_partition --partition-name factory --input $bin
if ($LASTEXITCODE -ne 0) { Write-Error "parttool write_partition fallo"; exit 1 }

Write-Host "OK - $DevId provisto. El equipo empezara a reportar tras reiniciar." -ForegroundColor Green
