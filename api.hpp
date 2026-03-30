#ifndef API_H
#define API_H

#include "Arduino.h"
#include "UpiData.hpp"

struct PaymentStatus {
    bool status;
    String bank_rrn;
    String payment_date;
    int token_id;
};

UpiData fetch_upi_data(uint32_t amount, String order_id, String detail_data_json);
PaymentStatus check_status(String tx_id);
bool validate_api_cred(String api_key, String api_username, String pos_name);

#endif