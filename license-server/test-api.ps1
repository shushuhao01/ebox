# eBox license server full-chain test (local integration)
$ErrorActionPreference = 'Stop'
$base = 'http://127.0.0.1:3008'

function Call($method, $path, $body, $token) {
  $headers = @{}
  if ($token) { $headers['Authorization'] = "Bearer $token" }
  if ($null -ne $body) {
    $json = $body | ConvertTo-Json -Depth 5
    return Invoke-RestMethod -Uri "$base$path" -Method $method -Headers $headers -ContentType 'application/json' -Body $json
  }
  return Invoke-RestMethod -Uri "$base$path" -Method $method -Headers $headers
}

$pass = 0; $fail = 0
function Check($name, $cond, $detail) {
  if ($cond) { $script:pass++; Write-Host "[PASS] $name" }
  else { $script:fail++; Write-Host "[FAIL] $name -> $detail" }
}

Write-Host '===== 1. admin login ====='
$r = Call 'POST' '/api/admin/auth/login' @{ username = 'admin'; password = 'admin888' }
$token = $r.data.token
Check 'login ok' ($r.code -eq 0 -and $token) $r.msg

Write-Host '===== 2. generate key (30d bound) ====='
$r = Call 'POST' '/api/admin/keys/generate' @{ durationSec = 2592000; bound = $true; unbindMax = 3; count = 1; batchName = 'batch-test' } $token
$code = $r.data.codes[0]
Check 'generate ok' ($r.code -eq 0 -and $code) $r.msg
Write-Host "   code: $code"

Write-Host '===== 3. client activate (first) ====='
$fp = 'aaaaaaaaaaaaaaaa'
$ts = [int][double]::Parse((Get-Date -UFormat %s))
$r = Call 'POST' '/api/v1/activate' @{ code = $code; machineFp = $fp; appVersion = '2.8.0'; os = 'Windows 10'; timestamp = $ts; nonce = 'n1' }
Check 'activate online registered' ($r.code -eq 0 -and $r.data.online -eq $true -and $r.data.bound -eq $true) ($r | ConvertTo-Json -Depth 3)
Write-Host "   expireType=$($r.data.expireType) serverTime=$($r.data.serverTime)"

Write-Host '===== 4. heartbeat ====='
$ts = [int][double]::Parse((Get-Date -UFormat %s))
$r = Call 'POST' '/api/v1/heartbeat' @{ code = $code; machineFp = $fp; appVersion = '2.8.0'; timestamp = $ts; nonce = 'n2' }
Check 'heartbeat ok' ($r.code -eq 0 -and $r.data.status -eq 'ok') ($r | ConvertTo-Json -Depth 3)
Write-Host "   graceUntil=$($r.data.graceUntil)"

Write-Host '===== 5. second device should be rejected ====='
$ts = [int][double]::Parse((Get-Date -UFormat %s))
$r = Call 'POST' '/api/v1/activate' @{ code = $code; machineFp = 'bbbbbbbbbbbbbbbb'; timestamp = $ts; nonce = 'n3' }
Check 'bound key rejected on 2nd machine' ($r.code -eq 0 -and $r.data.exceeded -eq $true) ($r | ConvertTo-Json -Depth 3)

Write-Host '===== 6. admin revoke ====='
$list = Invoke-RestMethod -Uri "$base/api/admin/keys?pageSize=1" -Headers @{Authorization="Bearer $token"}
$keyId = $list.data.list[0].id
$r = Call 'POST' "/api/admin/keys/$keyId/revoke" @{ reason = 'refund' } $token
Check 'revoke ok' ($r.code -eq 0) $r.msg

Write-Host '===== 7. heartbeat after revoke -> revoked ====='
$ts = [int][double]::Parse((Get-Date -UFormat %s))
$r = Call 'POST' '/api/v1/heartbeat' @{ code = $code; machineFp = $fp; timestamp = $ts; nonce = 'n4' }
Check 'heartbeat returns revoked' ($r.code -eq 0 -and $r.data.status -eq 'revoked') ($r | ConvertTo-Json -Depth 3)

Write-Host '===== 8. unbind / switch code ====='
$ts = [int][double]::Parse((Get-Date -UFormat %s))
$r = Call 'POST' '/api/v1/unbind' @{ code = $code; machineFp = $fp; timestamp = $ts; nonce = 'n5' }
if ($r.data.newCode) {
  $newCode = $r.data.newCode
  Check 'switch code issued' ($r.code -eq 0) $r.msg
  Write-Host "   switch code: $newCode"
  $ts = [int][double]::Parse((Get-Date -UFormat %s))
  $r2 = Call 'POST' '/api/v1/activate' @{ code = $newCode; machineFp = 'cccccccccccccccc'; timestamp = $ts; nonce = 'n6' }
  Check 'switch code activates on new machine' ($r2.code -eq 0 -and $r2.data.online -eq $true) ($r2 | ConvertTo-Json -Depth 3)
} else {
  Check 'switch code issued' $false ($r | ConvertTo-Json -Depth 3)
}

Write-Host '===== 9. stats overview ====='
$r = Call 'GET' '/api/admin/stats/overview' $null $token
Check 'stats ok' ($r.code -eq 0) ($r | ConvertTo-Json -Depth 3)
$r | ConvertTo-Json -Depth 3

Write-Host ''
Write-Host "RESULT: PASS=$pass FAIL=$fail"
if ($fail -gt 0) { exit 1 } else { exit 0 }
