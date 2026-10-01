#include "ocpp/application/central_system.hpp"
#include "ocpp/billing/settlement.hpp"

namespace ocpp {
Json Service::settle_sandbox(const Json &request) {
    const auto result = sandbox_settlement(request);
    const auto id = request.at("caseId").get<std::string>();
    auto db = database_.acquire();
    db.begin();
    db.execute(
        "INSERT IGNORE INTO cpp_billing_sandbox(case_id,request_body,response_body) VALUES(?,?,?)",
        {id, request.dump(), result.dump()});
    const auto rows = db.execute("SELECT request_body,response_body FROM cpp_billing_sandbox WHERE "
                                 "case_id=? FOR UPDATE",
                                 {id})
                          .rows;
    if (rows.size() != 1 || rows[0]["request_body"] != request.dump())
        throw ProtocolError("ProtocolError", "Sandbox case ID reused with different input");
    const auto stored = Json::parse(rows[0]["response_body"].get<std::string>());
    db.commit();
    return stored;
}
} // namespace ocpp
