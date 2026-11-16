# Запуск примера: приложение + (по умолчанию) тестовый отправитель.
#   .\run.ps1                 — окно + генератор потока
#   .\run.ps1 -NoSender       — только приёмник (например, с реального борта)
param(
    [int]$Port = 45454,
    [switch]$NoSender,
    [string]$QtDir = "C:\msys64\ucrt64"
)

$ErrorActionPreference = "Stop"
$env:PATH = "$QtDir\bin;" + $env:PATH

$exe = Join-Path $PSScriptRoot "build\simple-io-app.exe"
if (-not (Test-Path $exe)) {
    throw "Нет $exe — сначала соберите: .\build.ps1"
}

$sender = $null
if (-not $NoSender) {
    # Нужен именно тот Python, где стоит Pillow: на машине их может быть
    # несколько (python.exe, py-launcher, заглушка из WindowsApps).
    # Проверку делаем при ErrorActionPreference=Continue: иначе PowerShell 5.1
    # превращает любой вывод в stderr в исключение и обрывает скрипт.
    $py = $null
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    foreach ($name in @('python', 'py')) {
        $cmd = Get-Command $name -ErrorAction SilentlyContinue
        if (-not $cmd -or $cmd.Source -like '*WindowsApps*') { continue }
        & $cmd.Source -c "import PIL" *> $null
        if ($LASTEXITCODE -eq 0) { $py = $cmd.Source; break }
    }
    $ErrorActionPreference = $prevEap

    if ($py) {
        # -u: не буферизовать вывод, иначе лог отправителя пуст до его выхода.
        # Вывод в файлы: без этого у скрытого процесса может не быть stdout.
        $sender = Start-Process -FilePath $py `
            -ArgumentList "-u", (Join-Path $PSScriptRoot "tools\fake_sender.py"), "--port", $Port `
            -PassThru -WindowStyle Minimized `
            -RedirectStandardOutput (Join-Path $PSScriptRoot "sender.log") `
            -RedirectStandardError (Join-Path $PSScriptRoot "sender.err.log")
        Write-Host "Тестовый отправитель запущен (PID $($sender.Id)), лог: sender.log"
        Start-Sleep -Milliseconds 1500
        if ($sender.HasExited) {
            Write-Warning "Отправитель завершился — смотрите sender.err.log"
        }
    } else {
        Write-Warning "Не найден Python с Pillow (pip install pillow) — окно будет без данных"
    }
}

try {
    # Start-Process -Wait, а не "& $exe": приложение собрано как GUI, и в
    # Windows вызов через & возвращает управление сразу, из-за чего скрипт
    # завершался и в finally глушил отправителя.
    $app = Start-Process -FilePath $exe -ArgumentList "--port", $Port -PassThru -Wait
} finally {
    if ($sender -and -not $sender.HasExited) {
        Stop-Process -Id $sender.Id -Force -ErrorAction SilentlyContinue
    }
}
