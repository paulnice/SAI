#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <fstream>
#include <sstream>
#include <set>
#include <iostream>
#include <getopt.h>
#include <assert.h>
#include <signal.h>

#include <thread>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "switch_sai_rpc.h"
#include "switch_sai_rpc_server.h"

#define UNREFERENCED_PARAMETER(P)   (P)

extern "C" {
#include "sai.h"
#include "saistatus.h"
}


#define SWITCH_SAI_THRIFT_RPC_SERVER_PORT 9092

sai_switch_api_t* sai_switch_api;

std::map<std::string, std::string> gProfileMap;
std::map<std::set<int>, std::string> gPortMap;

void on_switch_state_change(_In_ sai_switch_oper_status_t switch_oper_status)
{
}

void on_fdb_event(_In_ uint32_t count,
                  _In_ sai_fdb_event_notification_data_t *data)
{
}

void on_port_state_change(_In_ uint32_t count,
                          _In_ sai_port_oper_status_notification_t *data)
{
}

void on_port_event(_In_ uint32_t count,
                   _In_ sai_port_event_notification_t *data)
{
}

void on_shutdown_request()
{
}

void on_packet_event(_In_ const void *buffer,
                     _In_ sai_size_t buffer_size,
                     _In_ uint32_t attr_count,
                     _In_ const sai_attribute_t *attr_list)
{
}

sai_switch_notification_t switch_notifications = {
    on_switch_state_change,
    on_fdb_event,
    on_port_state_change,
    on_port_event,
    on_shutdown_request,
    on_packet_event
};

// Profile services
/* Get variable value given its name */
const char* test_profile_get_value(
        _In_ sai_switch_profile_id_t profile_id,
        _In_ const char* variable)
{
    UNREFERENCED_PARAMETER(profile_id);

    if (variable == NULL)
    {
        printf("variable is null\n");
        return NULL;
    }

    std::map<std::string, std::string>::const_iterator it = gProfileMap.find(variable);
    if (it == gProfileMap.end())
    {
        printf("%s: NULL\n", variable);
        return NULL;
    }

    return it->second.c_str();
}

std::map<std::string, std::string>::iterator gProfileIter = gProfileMap.begin();
/* Enumerate all the K/V pairs in a profile.
   Pointer to NULL passed as variable restarts enumeration.
   Function returns 0 if next value exists, -1 at the end of the list. */
int test_profile_get_next_value(
        _In_ sai_switch_profile_id_t profile_id,
        _Out_ const char** variable,
        _Out_ const char** value)
{
    UNREFERENCED_PARAMETER(profile_id);

    if (value == NULL)
    {
        printf("resetting profile map iterator");

        gProfileIter = gProfileMap.begin();
        return 0;
    }

    if (variable == NULL)
    {
        printf("variable is null");
        return -1;
    }

    if (gProfileIter == gProfileMap.end())
    {
        printf("iterator reached end");
    return -1;
}

    *variable = gProfileIter->first.c_str();
    *value = gProfileIter->second.c_str();

    printf("key: %s:%s", *variable, *value);

    gProfileIter++;

    return 0;
}

const service_method_table_t test_services = {
    test_profile_get_value,
    test_profile_get_next_value
};

#ifdef BRCMSAI
void sai_diag_shell()
{
    sai_status_t status;

    while (true)
    {
        sai_attribute_t attr;
        attr.id = SAI_SWITCH_ATTR_CUSTOM_RANGE_BASE + 1;
        status = sai_switch_api->set_switch_attribute(&attr);
        if (status != SAI_STATUS_SUCCESS)
        {
            return;
        }

        sleep(1);
    }
}
#endif

struct cmdOptions
{
    std::string profileMapFile;
    std::string portMapFile;
};

cmdOptions handleCmdLine(int argc, char **argv)
{

    cmdOptions options = {};

    while(true)
    {
        static struct option long_options[] =
        {
            { "profile",          required_argument, 0, 'p' },
            { "portmap",          required_argument, 0, 'f' },
            { 0,                  0,                 0,  0  }
        };

        int option_index = 0;

        int c = getopt_long(argc, argv, "p:f:", long_options, &option_index);

        if (c == -1)
            break;

        switch (c)
        {
            case 'p':
                printf("profile map file: %s", optarg);
                options.profileMapFile = std::string(optarg);
                break;

            case 'f':
                printf("port map file: %s", optarg);
                options.portMapFile = std::string(optarg);
                break;

            default:
                printf("getopt_long failure");
                exit(EXIT_FAILURE);
        }
    }

    return options;
}

void handleProfileMap(const std::string& profileMapFile)
{

    if (profileMapFile.size() == 0)
        return;

    std::ifstream profile(profileMapFile);

    if (!profile.is_open())
    {
        printf("failed to open profile map file: %s : %s\n", profileMapFile.c_str(), strerror(errno));
        exit(EXIT_FAILURE);
    }

    std::string line;

    while(getline(profile, line))
    {
        if (line.size() > 0 && (line[0] == '#' || line[0] == ';'))
            continue;

        size_t pos = line.find("=");

        if (pos == std::string::npos)
        {
            printf("not found '=' in line %s\n", line.c_str());
            continue;
        }

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        gProfileMap[key] = value;

        printf("insert: %s:%s\n", key.c_str(), value.c_str());
    }
}

int
main(int argc, char* argv[])
{
    int rv = 0;

    auto options = handleCmdLine(argc, argv);
    handleProfileMap(options.profileMapFile);
    handlePortMap(options.portMapFile);

    sai_api_initialize(0, (service_method_table_t *)&test_services);
    sai_api_query(SAI_API_SWITCH, (void**)&sai_switch_api);

    sai_status_t status = sai_switch_api->initialize_switch(0, "", "", &switch_notifications);
    if (status != SAI_STATUS_SUCCESS)
    {
        exit(EXIT_FAILURE);
    }

#ifdef BRCMSAI
    std::thread bcm_diag_shell_thread = std::thread(sai_diag_shell);
    bcm_diag_shell_thread.detach();
#endif

    start_sai_thrift_rpc_server(SWITCH_SAI_THRIFT_RPC_SERVER_PORT);

    sai_log_set(SAI_API_SWITCH, SAI_LOG_NOTICE);
    sai_log_set(SAI_API_FDB, SAI_LOG_NOTICE);
    sai_log_set(SAI_API_PORT, SAI_LOG_NOTICE);
    sai_log_set(SAI_API_VLAN, SAI_LOG_NOTICE);
    sai_log_set(SAI_API_ROUTE, SAI_LOG_NOTICE);
    sai_log_set(SAI_API_VIRTUAL_ROUTER, SAI_LOG_NOTICE);
    sai_log_set(SAI_API_ROUTER_INTERFACE, SAI_LOG_NOTICE);
    sai_log_set(SAI_API_NEXT_HOP, SAI_LOG_NOTICE);
    sai_log_set(SAI_API_NEXT_HOP_GROUP, SAI_LOG_NOTICE);
    sai_log_set(SAI_API_NEIGHBOR, SAI_LOG_NOTICE);
    sai_log_set(SAI_API_ACL, SAI_LOG_NOTICE);
    sai_log_set(SAI_API_MIRROR, SAI_LOG_NOTICE);
    sai_log_set(SAI_API_LAG, SAI_LOG_NOTICE);
    sai_log_set(SAI_API_BUFFERS, SAI_LOG_NOTICE);

    while (1) pause();

    return rv;
}
