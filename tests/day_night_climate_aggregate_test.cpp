#include "cattle_climate/event_store.hpp"

#include <cstdio>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace cattle_climate;
    const std::string db_path = "test_day_night.sqlite";
    std::remove(db_path.c_str());
    EventStoreSettings settings{db_path};
    initialize_event_store(settings);
    log_event_record(settings, EventLogRecord{"2023-01-01 07:10:00","validate",10,20,30,0.1,0.2,0.3,0.6,1.0,450,3,5,18,0.60,1.1,1.2,100,4000,4,20});
    log_event_record(settings, EventLogRecord{"2023-01-01 08:10:00","validate",10,20,30,0.1,0.2,0.3,0.6,1.0,450,3,5,20,0.65,1.3,1.2,100,4300,4,20});
    log_event_record(settings, EventLogRecord{"2023-01-01 19:10:00","validate",10,20,30,0.1,0.2,0.3,0.6,1.0,450,3,5,14,0.70,0.7,1.0,100,3900,4,20});
    auto rows = query_day_night_climate_aggregate(settings, "2023-01-01", "validate");
    if (rows.size() != 2) throw std::runtime_error("Expected day and night rows");
    if (rows[0].period_label != "day") throw std::runtime_error("Expected day first");
    if (rows[0].sample_count != 2) throw std::runtime_error("Unexpected day sample count");
    if (rows[1].period_label != "night") throw std::runtime_error("Expected night second");
    if (rows[1].sample_count != 1) throw std::runtime_error("Unexpected night sample count");
    std::cout << "day_night_climate_aggregate_test=ok\n";
    return 0;
}
