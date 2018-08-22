//
// Created by eric on 18-8-16.
//
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <cassert>
#include <unistd.h>
#define TEST_NZ(x) do { if ( (x)) die("error: " #x " failed (returned non-zero)." ); } while (0)
#define TEST_Z(x)  do { if (!(x)) die("error: " #x " failed (returned zero/null)."); } while (0)
using namespace std;

struct context
{
    struct ibv_context *ctx;
    struct ibv_pd *pd;
    struct ibv_cq *cq;
    struct ibv_comp_channel *comp_channel;

    pthread_t cq_poller_thread;
};
struct connection
{
    struct ibv_qp *qp;

    struct ibv_mr *recv_mr;
    struct ibv_mr *send_mr;

    char *recv_region;
    char *send_region;
};
static context *s_ctx;
int BUFFER_SIZE=-1;

// For send
ibv_send_wr wr,*bad_wr;
ibv_sge sge;
ibv_qp *global_qp;

void die(const char *reason)
{
    fprintf(stderr, "%s\n", reason);
    exit(EXIT_FAILURE);
}

void prepare_send_wr(connection *conn)
{
    memset(&wr,0,sizeof(wr));
    wr.opcode=IBV_WR_SEND;
    wr.sg_list=&sge;
    wr.num_sge=1;
    wr.send_flags=IBV_SEND_SIGNALED;//IBV_SEND_INLINED

    sge.lkey=conn->send_mr->lkey;
    sge.length=BUFFER_SIZE;
    sge.addr=(uintptr_t)conn->send_region;

    snprintf(conn->send_region, BUFFER_SIZE
            , "message from active/client side with pid %d", getpid());
    for (int i=1;i<BUFFER_SIZE-2;++i)
    {
        conn->send_region[i]=rand()%26+'a';
    }
    conn->send_region[0]='#';
    conn->send_region[BUFFER_SIZE-2]='#';
    conn->send_region[BUFFER_SIZE-1]=0;

    global_qp=conn->qp;
//    TEST_NZ(ibv_post_send(conn->qp,&wr,&bad_wr));

    cout<<"Sent msg: "<<conn->send_region<<endl;
}


void on_completion(ibv_wc* wc)
{
    TEST_Z(wc->status==IBV_WC_SUCCESS);
    if (wc->opcode == IBV_WC_SEND)
    {
        cout<<"Send wr successfully finished."<<endl;
    } else if (wc->opcode == IBV_WC_RECV)
    {
        TEST_NZ(ibv_post_send(global_qp,&wr,&bad_wr));//TODO:CHANGED
        cout<<"Recv wr successfully finished. The msg is: "
            <<((connection*)wc->wr_id)->recv_region<<endl;
    } else
    {
        cout<<"Unknown wc, op is "<<wc->opcode<<endl;
    }
}

void *poll_cq(void *ctx)
{
    ibv_cq *cq;
    ibv_wc wc;
    while (true)
    {
        ibv_get_cq_event(s_ctx->comp_channel,&cq,&ctx);
        ibv_ack_cq_events(cq,1);
        ibv_req_notify_cq(s_ctx->cq,0);

        while(ibv_poll_cq(s_ctx->cq,1,&wc))
        {
            on_completion(&wc);
        }
    }
    return nullptr;
}

void build_context(ibv_context *verbs)
{
    if (s_ctx)
    {
        if (s_ctx->ctx != verbs)
        {
            die("Different verbs");
        }
        return;
    }

    s_ctx=new context();
    s_ctx->ctx=verbs;
    TEST_Z(s_ctx->pd=ibv_alloc_pd(s_ctx->ctx));
    TEST_Z(s_ctx->comp_channel=ibv_create_comp_channel(s_ctx->ctx));
    TEST_Z(s_ctx->cq=ibv_create_cq(s_ctx->ctx,10,NULL,s_ctx->comp_channel,0));
    TEST_NZ(ibv_req_notify_cq(s_ctx->cq,0));
    cout<<"Requested cqe=10, real cqe="<<s_ctx->cq->cqe<<endl;

    pthread_t *thread=new pthread_t;
    TEST_NZ(pthread_create(thread, nullptr,poll_cq, nullptr));
    cout<<"Create pd cc cq thread successfully"<<endl;
}

void build_qp_attr(ibv_qp_init_attr* qp_init_attr)
{
    memset(qp_init_attr,0,sizeof(ibv_qp_init_attr));
    qp_init_attr->recv_cq=qp_init_attr->send_cq=s_ctx->cq;
    qp_init_attr->qp_type=IBV_QPT_RC;

    qp_init_attr->cap.max_recv_wr=10;
    qp_init_attr->cap.max_send_wr=10;
    qp_init_attr->cap.max_send_sge=1;
    qp_init_attr->cap.max_recv_sge=1;
}

void regist_memory(connection *conn)
{
    conn->recv_region=new char[BUFFER_SIZE];
    conn->send_region=new char[BUFFER_SIZE];
    TEST_Z(conn->recv_mr=ibv_reg_mr(s_ctx->pd,conn->recv_region,BUFFER_SIZE,
                                     IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE));
    TEST_Z(conn->send_mr=ibv_reg_mr(s_ctx->pd,conn->send_region,BUFFER_SIZE,
                                     IBV_ACCESS_LOCAL_WRITE));//|IBV_ACCESS_REMOTE_WRITE));
}

void post_recv(connection* conn)
{
    ibv_recv_wr wr,*bad_wr= nullptr;
    ibv_sge sge;
    wr.wr_id=(uintptr_t)conn;
    wr.next= nullptr;
    wr.num_sge=1;
    wr.sg_list=&sge;

    sge.addr=(uintptr_t)conn->recv_region;
    sge.length=BUFFER_SIZE;
    sge.lkey=conn->recv_mr->lkey;

    TEST_NZ(ibv_post_recv(conn->qp, &wr, &bad_wr));
}

int on_request(rdma_cm_id *id)
{
    cout<<"Recv connection request"<<endl;

    ibv_qp_init_attr qp_init_attr;
    connection * conn;
    build_context(id->verbs);
    build_qp_attr(&qp_init_attr);
    TEST_NZ(rdma_create_qp(id,s_ctx->pd,&qp_init_attr));
    id->context=conn=new connection();
    conn->qp=id->qp;
    regist_memory(conn);
    post_recv(conn);
    prepare_send_wr(conn);//TODO:CHANGED


    rdma_conn_param param;
    memset(&param,0,sizeof(param));
    TEST_NZ(rdma_accept(id,&param));

    cout<<"Recv connection ACCEPTED"<<endl;
    return 0;
}

int on_connection(void *context)
{
    cout<<"Connected"<<endl;
    connection* conn=(connection*) context;
//    ibv_send_wr wr,*bad_wr;
//    ibv_sge sge;
    return 0;
}

int on_disconnection(rdma_cm_id *id)
{
    connection* conn=(connection*)id->context;
    ibv_dereg_mr(conn->send_mr);
    ibv_dereg_mr(conn->recv_mr);
    ibv_destroy_qp(conn->qp);
    delete(conn->send_region);
    delete(conn->recv_region);
    delete(conn);
    rdma_destroy_id(id);
    return 1;
}

int on_event(const rdma_cm_event &event)
{
    cout<<"Event recv"<<endl;
    int ret;
    if (event.event == RDMA_CM_EVENT_CONNECT_REQUEST)
    {
        ret=on_request(event.id);
    } else if (event.event == RDMA_CM_EVENT_ESTABLISHED)
    {
        ret=on_connection(event.id->context);
    } else if (event.event==RDMA_CM_EVENT_DISCONNECTED){
        ret=on_disconnection(event.id);
    } else
        die("Unknown event");
    return ret;
}

int main(int argv,char ** argc)
{
    if (argv!=2)
    {
        printf("./... msg_size\n");
        return -1;
    }
    srand(1024);
    BUFFER_SIZE=atoi(argc[1]);
    printf("Buffer size= %d\n",BUFFER_SIZE);
    rdma_event_channel* ec= nullptr;
    rdma_cm_id* id= nullptr;
    sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    TEST_Z(ec=rdma_create_event_channel());
    TEST_NZ(rdma_create_id(ec,&id, nullptr,RDMA_PS_TCP));
    addr.sin_family=AF_INET;
    addr.sin_port=htons(33333);
    TEST_NZ(rdma_bind_addr(id,(sockaddr *)&addr));
    TEST_NZ(rdma_listen(id,10));
    cout<<"Listen at port "<<ntohs(rdma_get_src_port(id))<<endl;
    cout<<"port without ntohs "<<rdma_get_src_port(id)<<endl;

    rdma_cm_event event_copy,*event;
    while (!rdma_get_cm_event(ec, &event))
    {
        event_copy=*event;
        rdma_ack_cm_event(event);
        TEST_NZ(on_event(event_copy));
    }

    rdma_destroy_id(id);
    rdma_destroy_event_channel(ec);
}
