$ErrorActionPreference='Stop'
$names=@('drogon','trantor','mariadb-connector-c','zlib','jsoncpp','nlohmann-json','json-schema-validator')
$commits=@('4c5430757ea5451a7c38fbbef4b4bef7dbb47f2f','63a4e5e164e219dc3bf30cdbfa1462ae5602fa97','c61bdb5ac1cd1b41210dd57bb14fa377e555ce0c','da607da739fa6047df13e66a2af6b8bec7c2a498','89e2973c754a9c02a49974d839779b151e95afd6','55f93686c01528224f448c19128836e7df245f72','349cba9f7e3cb423bbc1811bdd9f6770f520b468')
$queries=@($commits | ForEach-Object {@{commit=$_}})
$body=@{queries=$queries} | ConvertTo-Json -Depth 5
$result=Invoke-RestMethod -Uri 'https://api.osv.dev/v1/querybatch' -Method Post -ContentType 'application/json' -Body $body
for($i=0;$i -lt $names.Length;$i++){
  $ids=@($result.results[$i].vulns | ForEach-Object {$_.id})
  [PSCustomObject]@{dependency=$names[$i];commit=$commits[$i];advisories=($ids -join ',')}
}
# This is an advisory lookup, not reachability analysis, a complete SBOM or a pentest.
