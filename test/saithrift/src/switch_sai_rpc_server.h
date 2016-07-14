#ifndef SWITCH_SAI_RPC_SERVER_H
#define SWITCH_SAI_RPC_SERVER_H

extern "C" {
    int start_sai_thrift_rpc_server(int port);
}

void handlePortMap(const std::string& portMapFile);

#endif // SWITCH_SAI_RPC_SERVER_H
