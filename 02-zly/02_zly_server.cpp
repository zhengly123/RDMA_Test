//
// Created by eric on 18-8-18.
//

#include <netdb.h>
#include "common.h"
using namespace std;

int on_event(rdma_cm_event event);
int on_request(rdma_cm_id *id);
int on_connection(rdma_cm_id *id);
int on_disconnection(rdma_cm_id *id);

int main(int argc,char ** argv)
{
    if (argc != 2)
    {
        print_usage();
        return 0;
    }
    if (argv[1][0]=='R')
        set_mode(MODE::READ),printf("Mode RDMA read\n");
    else
        set_mode(MODE::WRITE),printf("Mode RDMA write\n");
    rdma_event_channel *ec;
    rdma_cm_id *id;
    sockaddr_in addr;

    TEST_NZ(ec=rdma_create_event_channel());
    TEST_Z(rdma_create_id(ec,&id, nullptr,RDMA_PS_TCP));
    memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;
    addr.sin_port=htons(33333);
    TEST_Z(rdma_bind_addr(id,(sockaddr *)&addr));
    TEST_Z(rdma_listen(id,10));
    printf("Listening at port %u\n",ntohs(rdma_get_src_port(id)));

    rdma_cm_event *event,event_copy;
    while (!rdma_get_cm_event(ec, &event))
    {
        event_copy=*event;
        rdma_ack_cm_event(event);
        if (on_event(event_copy))
            break;
    }
    rdma_destroy_id(id);
    rdma_destroy_event_channel(ec);
    return 0;
}

int on_event(rdma_cm_event event)
{
    TEST_Z(event.status);
    int ret=0;
    if (event.event == RDMA_CM_EVENT_CONNECT_REQUEST)
    {
        ret=on_request(event.id);
    } else if (event.event == RDMA_CM_EVENT_ESTABLISHED)
    {
        ret=on_connection(event.id);
    } else if (event.event == RDMA_CM_EVENT_DISCONNECTED)
    {
        ret=on_disconnection(event.id);
    } else
        die("Unknown event");
    return ret;
}

int on_request(rdma_cm_id *id)
{
    printf("On connect request\n");
    build_id(id);

    rdma_conn_param param;
    memset(&param,0,sizeof(param));
//    param.initiator_depth = param.responder_resources = 1;//RDMA属性
//    param.rnr_retry_count = 7; /* infinite retry */
    build_param(&param);
    rdma_accept(id,&param);
    return 0;
}

int on_connection(rdma_cm_id *id)
{
    printf("On connection established\n");
    send_mr(id->context);
    return 0;
}

int on_disconnection(rdma_cm_id *id)
{
    printf("On disconnected\n");
    clear(id->context);
    return 0;
}
