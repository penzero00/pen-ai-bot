Add-Type -AssemblyName System.Speech
$out = Join-Path $PSScriptRoot "..\firmware\src\hello_raw.wav"
$s = New-Object System.Speech.Synthesis.SpeechSynthesizer
$s.Rate = -1
$s.Volume = 100
$s.SetOutputToWaveFile($out)
$s.Speak("Hello")
$s.Dispose()
Write-Host "Wrote $out"
