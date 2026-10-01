package de.rwth.idsg.steve.repository;

import de.rwth.idsg.steve.repository.dto.Transaction;
import de.rwth.idsg.steve.repository.dto.TransactionDetails;
import de.rwth.idsg.steve.web.dto.TransactionQueryForm;

import java.io.Writer;
import java.util.List;

public interface TransactionRepository {
    List<Transaction> getTransactions(TransactionQueryForm form);

    void writeTransactionsCSV(TransactionQueryForm form, Writer writer);

    List<Integer> getActiveTransactionIds(String chargeBoxId);

    TransactionDetails getDetails(int transactionPk, boolean firstArrivingMeterValueIfMultiple);

    default TransactionDetails getDetails(int transactionPk) {
        return getDetails(transactionPk, true);
    }
}
