# External Endpoints

This document lists outbound HTTP endpoints and related external database links used by `old_ocpp_server`.

Last verified: 2026-05-20.

## Audit Scope

Checked source files under `src/main/java`, production config under `src/main/resources/config/prod/main.properties`,
and build configuration in `pom.xml`.
The audit looked for:

```text
String endpoint =
new URL(...)
openConnection(...)
HttpURLConnection
DriverManager.getConnection(...)
RestTemplate
WebClient / HttpClient / OkHttp
http:// / https:// / ws:// / wss:// / jdbc:mysql
```

Result summary:

| Category | Count | Notes |
| --- | ---: | --- |
| Active `String endpoint =` assignments | 10 | 9 billing API calls, 1 local self-call. |
| Active billing API HTTP calls | 9 | All use `BILLING_API_BASE_URL`. |
| Active local HTTP calls | 1 | Hardcoded `localhost:5002` self-call. |
| Active billing DB connections | 6 | All use `BILLING_DB_*`. |
| Active OCPP DB connection setup | 1 | Hikari uses `db.url`. |
| Build-time OCPP DB plugin connections | 2 | Flyway and jOOQ use `${jdbcUrl}`, which now comes from `db.url`. |
| Internet availability probe list | 6 URLs | In `InternetChecker`. |
| Release-check HTTP client | 1 class | `GithubReleaseCheckService`, but `API_URL` is currently empty. |

## Config Sources

Defined in `src/main/resources/config/prod/main.properties`:

```properties
db.schema = ocpptest
db.url = jdbc:mysql://localhost:3306/ocpptest?useSSL=true&serverTimezone=UTC
db.user = barry
db.password = pinkywinky

billing.api.base-url = https://bluestone-codex.com:5005/

billing.db.url = jdbc:mysql://localhost:3306/billing?useSSL=false
billing.db.user = blue
billing.db.password = bluestone88
```

Java reads these through `SteveConfiguration`:

```java
CONFIG.getDb().getUrl()
CONFIG.getDb().getUserName()
CONFIG.getDb().getPassword()
CONFIG.getBillingApi().getBaseUrl()
CONFIG.getBillingDb().getUrl()
CONFIG.getBillingDb().getUserName()
CONFIG.getBillingDb().getPassword()
```

## Billing API HTTP Calls

All paths below are relative to `billing.api.base-url`.

| Method | Endpoint | Source | Trigger / Payload |
| --- | --- | --- | --- |
| `POST` | `/transaction/authentication/card` | `OcppTagService.java:136` | During card authorization when status is accepted/invalid and context is authorize request. Payload: `chargebox`, `tag`. |
| `POST` | `/transaction/schneider/clear-start` | `OcppTagService.java:227` | Schneider brand start-event cleanup. Payload: billing `chargebox` id. |
| `POST` | `/transaction/clear-fault/availiable` | `CentralSystemService16_Service.java:225` | Connector status becomes `Available`. Payload: `chargebox`, `connector`. |
| `POST` | `/transaction/receipt-short` | `CentralSystemService16_Service.java:495` | After parking/transaction update. Payload: `transaction`. |
| `POST` | `/transaction/clear-success/preparing` | `CentralSystemService16_Service.java:567` | Connector status becomes `Preparing`. Payload: `chargebox`, `connector`. |
| `POST` | `/transaction/suspendev/check/donechargingornot` | `CentralSystemService16_Service.java:619` | Connector status becomes `SuspendEV`. Payload: `chargebox`, `connector`. |
| `POST` | `/transaction/initial/transactionid` | `OcppServerRepositoryImpl.java:484` | After transaction start insert. Payload: `transactionId`, `connectorPk`. |
| `POST` | `/{companyName}/{chargebox}/transaction/done` | `OcppServerRepositoryImpl.java:698` | After transaction stop/update. Payload: `leftoverEnergy`, `id`. |
| `POST` | `/transaction/initial/connectorid` | `OcppServerRepositoryImpl.java:906` | When a new connector is inserted. Payload: `chargebox`, `connector`, `connectorGun`. |

## Local/Internal HTTP Call

This is a local call back into the OCPP app, not the billing API:

| Method | Endpoint | Source | Purpose |
| --- | --- | --- | --- |
| `GET` | `http://localhost:5002/develop/dev/{chargeBoxIdentity}/stopSession/{uniqueTag}` | `OcppServerRepositoryImpl.java:329` | Stops a charging session when meter value reaches the paid energy target. |

This is the only active endpoint assignment that does not use config yet.

## Internet Availability Checks

`InternetChecker` probes these URLs with `HttpURLConnection` to determine whether the server has internet access:

```text
https://treibhaus.informatik.rwth-aachen.de/heartbeat/
https://github.com
https://www.wikipedia.org
https://www.google.com
https://www.apple.com
https://www.facebook.com
```

## Release Check Client

`GithubReleaseCheckService` constructs a `RestTemplate` and calls:

```java
restTemplate.getForObject(API_URL, ReleaseResponse.class)
```

Current value:

```java
private static final String API_URL = ""; //Provide Update URL
```

So the class is capable of an outbound release-check request, but no external release URL is currently configured in source.

## Database Links

The OCPP app has two database connection groups:

| Purpose | Config | Default |
| --- | --- | --- |
| OCPP database | `db.url`, `db.user`, `db.password`; `db.schema` is still used by Flyway/jOOQ schema selection | `jdbc:mysql://localhost:3306/ocpptest?useSSL=true&serverTimezone=UTC` |

`serverTimezone=UTC` is intentionally kept for OCPP DB compatibility because the project previously used this value in `pom.xml` for Flyway/jOOQ. Changing it can affect how MySQL `TIMESTAMP` values are interpreted by the JDBC driver.
| Billing database | `billing.db.url`, `billing.db.user`, `billing.db.password` | `jdbc:mysql://localhost:3306/billing?useSSL=false` |

Billing DB call sites:

```text
OcppTagService.java:190
CentralSystemService16_Service.java:177
CentralSystemService16_Service.java:336
OcppServerRepositoryImpl.java:263
OcppServerRepositoryImpl.java:586
OcppServerRepositoryImpl.java:654
```

OCPP DB setup:

```text
BeanConfiguration.java:73  Hikari uses CONFIG.getDb().getUrl()
```

Build-time DB usage:

```text
pom.xml:26   jdbcUrl = ${db.url}
pom.xml:303  Flyway uses ${jdbcUrl}
pom.xml:355  jOOQ codegen uses ${jdbcUrl}
```

## Search Notes

Current `String endpoint =` assignments in Java:

```text
CentralSystemService16_Service.java: 4 active billing API endpoints
OcppTagService.java: 2 active billing API endpoints
OcppServerRepositoryImpl.java: 3 active billing API endpoints, 1 local/internal endpoint
```

Stale commented endpoint examples were removed, so this search now shows active endpoint assignments only.

Hardcoded `https://bluestone-codex.com:5005/` and billing DB credentials remain only in `main.properties` and this documentation, not in Java source.
