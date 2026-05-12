@echo off
REM Balance checker using curl
setlocal

set ADDRESS=%1
if "%ADDRESS%"=="" (
    echo Usage: check_balance.bat ^<address^>
    exit /b 1
)

echo Querying balance for: %ADDRESS%
echo.

REM Query UTXOs owned by this address
curl -s -X POST "https://indexer.preview.midnight.network/api/v4/graphql" ^
  -H "Content-Type: application/json" ^
  -d "{\"query\":\"subscription { unshieldedTransactions(address: \\"%ADDRESS%\\") { ... on UnshieldedTransaction { createdUtxos { value owner } } } }\"]"

endlocal
