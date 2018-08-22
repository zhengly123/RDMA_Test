//
// Created by eric on 18-8-18.
//

#include <netdb.h>
#include <zconf.h>
#include "common.h"
using namespace std;
const int TIMEOUT_IN_MS=1000;

static int on_addr_resolved(struct rdma_cm_id *id);
static void on_completion(struct ibv_wc *wc);
static int on_connection(void *context);
static int on_disconnect(struct rdma_cm_id *id);
static int on_event(struct rdma_cm_event *event);
static int on_route_resolved(struct rdma_cm_id *id);


int main(int argc, char **argv)
{
    if (argc != 4)
    {
        print_usage();
        return 0;
    }
    if (argv[3][0]=='R')
        set_mode(MODE::READ),printf("Mode RDMA read\n");
    else
        set_mode(MODE::WRITE),printf("Mode RDMA write\n");
    isClient=true;
//    fflush(stdout);
    struct addrinfo *addr;
    struct rdma_cm_event *event = NULL;
    struct rdma_cm_id *conn= NULL;
    struct rdma_event_channel *ec = NULL;;

    printf("Start init\n");
    TEST_Z(getaddrinfo(argv[1], argv[2], NULL, &addr));

    TEST_NZ(ec = rdma_create_event_channel());
    TEST_Z(rdma_create_id(ec, &conn, NULL, RDMA_PS_TCP));
    TEST_Z(rdma_resolve_addr(conn, NULL, addr->ai_addr, TIMEOUT_IN_MS));

    freeaddrinfo(addr);

    while(!rdma_get_cm_event(ec,&event))
    {
        rdma_cm_event event_copy;
        event_copy=*event;
        rdma_ack_cm_event(event);
        if (on_event(&event_copy))
            break;
    }
    rdma_destroy_id(conn);
    rdma_destroy_event_channel(ec);
}


int on_addr_resolved(struct rdma_cm_id *id)
{
    struct ibv_qp_init_attr qp_attr;
    struct connection *conn;

    printf("address resolved.\n");

    build_id(id);

    TEST_Z(rdma_resolve_route(id, TIMEOUT_IN_MS));
    return 0;
}


int on_connection(void *context)
{
    printf("On connection established\n");
//    try_disconnect(context);//DEBUG
//    return 1;//DEBUG
    send_mr(context);
    return 0;
}

int on_disconnect(struct rdma_cm_id *id)
{
    printf("On disconnected\n");
    clear(id->context);

    return 1; /* exit event loop */
}

int on_event(struct rdma_cm_event *event)
{
    int r = 0;

    printf("On event waiting...\n");
    if (event->event == RDMA_CM_EVENT_ADDR_RESOLVED)
    {
        r = on_addr_resolved(event->id);
    }
    else if (event->event == RDMA_CM_EVENT_ROUTE_RESOLVED)
        r = on_route_resolved(event->id);
    else if (event->event == RDMA_CM_EVENT_ESTABLISHED)
        r = on_connection(event->id->context);
    else if (event->event == RDMA_CM_EVENT_DISCONNECTED)
        r = on_disconnect(event->id);
    else
        die("on_event: unknown event.");

    return r;
}

int on_route_resolved(struct rdma_cm_id *id)
{
    struct rdma_conn_param cm_params;

    printf("route resolved.\n");

    build_param(&cm_params);
    TEST_Z(rdma_connect(id, &cm_params));

    return 0;
}

