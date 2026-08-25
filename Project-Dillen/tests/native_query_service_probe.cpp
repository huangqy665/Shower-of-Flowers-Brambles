#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "native_query_service.h"

int main()
{
    core::CapabilityRegistry capabilities;
    core::NativeQueryService service;
    service.Configure(&capabilities);
    bool gateOpen = true;
    service.SetSafetyGate([&gateOpen]() -> std::shared_ptr<void>
    {
        return gateOpen
            ? std::static_pointer_cast<void>(std::make_shared<int>(1))
            : std::shared_ptr<void>{};
    });

    core::NativeQueryDescriptor descriptor;
    descriptor.operation = "probe.echo";
    descriptor.provider = "probe";
    std::vector<uint64_t> observedGenerations;
    std::vector<const void*> observedLeases;
    std::string error;
    if (!service.RegisterHandler(
            descriptor,
            [&observedGenerations, &observedLeases](
               const core::NativeQueryRequest& request,
               const core::NativeQueryExecutionContext& context,
               core::NativeQueryValue& value,
               std::string& queryError)
            {
                observedGenerations.push_back(
                    context.lifecycleGeneration
                );
                observedLeases.push_back(context.safetyLease.get());
                const core::NativeQueryValue* input = request.Find("value");
                if (!input)
                {
                    queryError = "value_missing";
                    return false;
                }
                value = *input;
                return true;
            },
            error
        ))
    {
        std::cerr << "Query registration failed: " << error << '\n';
        return 1;
    }
    service.SetGameplayContext(true, "CHI", 7);

    core::NativeQueryRequest request;
    request.operation = "PROBE.ECHO";
    request.arguments.emplace("value", core::NativeQueryValue(int64_t{42}));
    const core::NativeQueryResult result = service.ExecuteImmediate(
        request,
        1,
        11
    );
    int64_t value = 0;
    if (!result.Succeeded()
        || !core::NativeQueryValueToInteger(result.value, value)
        || value != 42)
    {
        std::cerr << "Query execution failed\n";
        return 2;
    }
    const core::NativeQueryResult wrongThread = service.ExecuteImmediate(
        request,
        2,
        12
    );
    if (wrongThread.status
        != core::NativeQueryStatus::WrongExecutionThread)
    {
        std::cerr << "Wrong query thread accepted\n";
        return 3;
    }
    service.ResetExecutionThread();
    if (!service.ExecuteImmediate(request, 2, 12).Succeeded())
    {
        std::cerr << "Query thread reset failed\n";
        return 4;
    }
    observedGenerations.clear();
    observedLeases.clear();
    core::NativeQueryRequest first = request;
    first.key = "first";
    core::NativeQueryRequest second = request;
    second.key = "second";
    second.arguments["value"] = core::NativeQueryValue(int64_t{9});
    const core::NativeQuerySnapshot snapshot = service.ExecuteSnapshot(
        {first, second},
        2,
        12
    );
    int64_t firstValue = 0;
    int64_t secondValue = 0;
    if (!snapshot.Succeeded()
        || snapshot.snapshotId == 0
        || snapshot.lifecycleGeneration != 7
        || snapshot.playerTag != "CHI"
        || snapshot.results.size() != 2
        || snapshot.results[0].key != "first"
        || snapshot.results[1].key != "second"
        || !core::NativeQueryValueToInteger(
            snapshot.results[0].value,
            firstValue
        )
        || !core::NativeQueryValueToInteger(
            snapshot.results[1].value,
            secondValue
        )
        || firstValue != 42
        || secondValue != 9
        || observedGenerations.size() != 2
        || observedGenerations[0] != 7
        || observedGenerations[1] != 7
        || observedLeases.size() != 2
        || observedLeases[0] == nullptr
        || observedLeases[0] != observedLeases[1])
    {
        std::cerr << "Same-generation query snapshot failed\n";
        return 5;
    }

    core::NativeQueryDescriptor generationDescriptor;
    generationDescriptor.operation = "probe.change_generation";
    generationDescriptor.provider = "probe";
    if (!service.RegisterHandler(
            std::move(generationDescriptor),
            [&service](const core::NativeQueryRequest&,
                       const core::NativeQueryExecutionContext&,
                       core::NativeQueryValue& value,
                       std::string& queryError)
            {
                value = core::NativeQueryValue(true);
                queryError.clear();
                service.SetGameplayContext(true, "CHI", 8);
                return true;
            },
            error
        ))
    {
        std::cerr << "Generation-change query registration failed\n";
        return 6;
    }
    core::NativeQueryRequest generationRequest;
    generationRequest.operation = "probe.change_generation";
    const core::NativeQuerySnapshot changed = service.ExecuteSnapshot(
        {generationRequest},
        2,
        12
    );
    if (changed.code != "native_query_lifecycle_changed"
        || !changed.results.empty())
    {
        std::cerr << "Cross-generation snapshot was exposed\n";
        return 7;
    }
    service.SetGameplayContext(true, "CHI", 7);
    gateOpen = false;
    if (service.ExecuteImmediate(request, 2, 12).code
        != "native_query_save_load_barrier_closed")
    {
        std::cerr << "Closed barrier allowed query\n";
        return 8;
    }
    if (!capabilities.Contains("query.probe.echo"))
    {
        std::cerr << "Query capability missing\n";
        return 9;
    }
    gateOpen = true;
    capabilities.Invalidate(
        "query.probe.echo",
        "probe_capability_invalidated"
    );
    const core::NativeQueryResult unavailable = service.ExecuteImmediate(
        request,
        2,
        12
    );
    if (unavailable.status
            != core::NativeQueryStatus::CapabilityUnavailable
        || unavailable.code != "native_query_capability_unavailable")
    {
        std::cerr << "Invalid query capability remained executable\n";
        return 10;
    }

    std::cout << "Native query service: passed\n";
    return 0;
}
