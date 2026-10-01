# Dependency review

Checked 2026-10-02 by OSV commit queries. This is a limited advisory lookup; it does not establish absence of vulnerabilities or replace binary/SBOM analysis. The custom transport patch is separate from upstream's revision and requires independent review.

| Dependency | Pinned source | OSV result / handling |
| --- | --- | --- |
| Drogon 1.9.13 | 4c5430757ea5451a7c38fbbef4b4bef7dbb47f2f | CVE-2026-94143 and CVE-2026-94144 concern ORM sort/filter construction. BUILD_ORM is OFF; this application uses neither Mapper/orderBy nor Criteria JSON construction. All SQL statements and identifier choices are internal constants and values are prepared parameters. This is a code-based reachability assessment, not an upstream fix or a clean vulnerability scan. |
| Trantor | 63a4e5e164e219dc3bf30cdbfa1462ae5602fa97 | No advisory returned for this commit |
| MariaDB Connector/C 3.4.5 | c61bdb5ac1cd1b41210dd57bb14fa377e555ce0c | No advisory returned for this commit; Windows connector uses Schannel, dynamically linked |
| zlib 1.3.2 | da607da739fa6047df13e66a2af6b8bec7c2a498 | Upgraded from 1.3.1 after findings; requery result recorded in VALIDATION.md |
| JsonCpp 1.9.6 | 89e2973c754a9c02a49974d839779b151e95afd6 | No advisory returned for this commit |
| nlohmann JSON 3.12.0 | 55f93686c01528224f448c19128836e7df245f72 | No advisory returned for this commit |
| json-schema-validator 2.3.0 | 349cba9f7e3cb423bbc1811bdd9f6770f520b468 | No advisory returned for this commit |

Review [Drogon issue 2575](https://github.com/drogonframework/drogon/issues/2575), [issue 2576](https://github.com/drogonframework/drogon/issues/2576), and the [zlib 1.3.2 audit-fix release](https://github.com/madler/zlib/releases/tag/v1.3.2). Original zlib lookup returned CVE-2026-22184 (standalone contrib utility) and CVE-2026-27171/CVE-2026-3381; 1.3.2 contains the audit fixes.

`scripts/scan-dependencies.ps1` repeats the source lookup. Linux builds currently use distribution-supplied JsonCpp/zlib/MariaDB/OpenSSL/UUID; those versions are outside this Windows source-pin lookup and need their own distribution vulnerability assessment. MariaDB Connector/C also contains its own compression dependency code; review vendored code in the binary SBOM rather than treating the source-pin query as exhaustive. The isolated Windows test database uses MariaDB 11.4.5 solely for regression tests, not as a recommended production server.

The Windows connector build explicitly uses the external zlib 1.3.2 rather than its bundled 1.2.13. PE dependency inspection confirmed both `ocpp_server.exe` and `libmariadb.dll` import the newly built `z.dll`. Dependency acquisition and license notices are under `.deps/`. Produce a deployable SBOM and package applicable notices before release; no complete deployment SBOM has been produced here.
