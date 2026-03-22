#include "cattle_climate/event_store.hpp"

#include <cstdio>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace cattle_climate;
    const std::string db_path = "test_controller_events.sqlite";
    std::remove(db_path.c_str());
    EventStoreSettings settings{db_path};
    initialize_event_store(settings);
    log_event_record(settings, EventLogRecord{"2023-01-01 00:10:00","validate",10,20,30,0.1,0.2,0.3,0.6,1.0,450,3,5,18,0.6,1.2,100,4000,4,20});
    log_event_record(settings, EventLogRecord{"2023-01-01 00:40:00","validate",15,25,35,0.2,0.1,0.4,0.7,1.1,451,3,5,19,0.6,1.0,120,4100,4,20});
    log_event_record(settings, EventLogRecord{"2023-01-01 01:15:00","operate",20,30,40,0.3,0.3,0.2,0.8,1.2,452,3,6,20,0.65,1.4,150,4200,5,21});

    const auto rows = query_estimator_aggregate(settings, "2023-01-01 00:00:00", "2023-01-01 01:59:59", 60, "validate");
    if (rows.size() != 1) throw std::runtime_error("Expected one aggregate row for validate mode.");
    if (rows.front().decision_count != 2) throw std::runtime_error("Unexpected decision_count.");
    if (rows.front().total_energy_kwh < 1.29 || rows.front().total_energy_kwh > 1.31) throw std::runtime_error("Unexpected total_energy_kwh.");
    if (rows.front().damper_energy_kwh < 0.29 || rows.front().damper_energy_kwh > 0.31) throw std::runtime_error("Unexpected damper_energy_kwh.");

    const auto all_rows = query_estimator_aggregate(settings, "2023-01-01 00:00:00", "2023-01-01 01:59:59", 60, "");
    if (all_rows.size() != 2) throw std::runtime_error("Expected two aggregate rows without mode filter.");
    std::cout << "event_store_aggregate_test=ok\n";
    return 0;
}
