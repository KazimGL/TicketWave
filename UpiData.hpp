#ifndef UPIDATA_H
#define UPIDATA_H

#include <Arduino.h>

class UpiData {
private:
    bool status;
    String upi_string;
    String transaction_id;
    String order_id;

public:
    // Fixed constructor to match api.cpp (4 arguments)
    UpiData(bool s, String upi, String txid, String oid) {
        status = s;
        upi_string = upi;
        transaction_id = txid;
        order_id = oid;
    }

    // --- GETTERS ---
    bool getStatus() { return status; }
    String getUpiString() { return upi_string; }
    String getTransactionId() { return transaction_id; }
    String getOrderId() { return order_id; }

    // --- SETTERS ---
    void setStatus(bool s) { status = s; }
    void setUpiString(String upi) { upi_string = upi; }
    void setTransactionId(String txid) { transaction_id = txid; }
    
    // Fixed: Added the missing setOrderId function
    void setOrderId(String oid) { order_id = oid; } 
};

#endif