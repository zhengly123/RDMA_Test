//
// Created by eric on 18-8-20.
//
#include <infiniband/verbs.h>
#include <zconf.h>
#include "common.h"

struct Msg{
    enum{
        MSG_MR,MSG_DONE
    }type;

    ibv_mr mr;
};

struct context{
    ibv_pd *pd;
    ibv_comp_channel *comp_channel;
    ibv_cq *cq;
    ibv_context *verbs;

    pthread_t pthread;
};
MODE mode;


struct connection{
    ibv_mr *mr_recv,*mr_send,*mr_rdma_for_remote,*mr_rdma_for_local;
    ibv_qp *qp;
    rdma_cm_id *id;
    int post_recv_cnt;
    bool connected= false;
    Msg *msg_send,*msg_recv;
    char *char_rdma_for_remote,*char_rdma_for_local;
    enum{
        INIT_SEND,MR_SENT,DONE_SENT
    }send_status;
    enum{
        INIT_RECV,MR_RECV,DONE_RECV
    }recv_status;
};

void build_s_ctx(ibv_context *pContext);
void* poll_cq(void *);

void build_qp(rdma_cm_id *id);

void build_mr(rdma_cm_id *id);

void rdma_operation(connection *conn);

void post_recv(void *pConn);

void send_mr(void *pConn);

void send_done(connection *conn);

static context *s_ctx;
bool isClient=false;

void build_id(rdma_cm_id *id)
{
    build_s_ctx(id->verbs);
    connection *conn=new connection;
    id->context=conn;
    memset(id->context,0,sizeof(connection));
    conn->id=id;
    build_qp(id);
    build_mr(id);

//    ibv_recv_wr wr,*bad_wr;
//    ibv_sge sge;
//    wr.wr_id=(uintptr_t)id->context;
//    wr.num_sge=1;
//    wr.sg_list=&sge;
//    wr.next= nullptr;
//    sge.length=sizeof(Msg);
//    sge.lkey=conn->mr_recv->lkey;
//    sge.addr=(uintptr_t)conn->msg_recv;
//    TEST_Z(ibv_post_recv(conn->qp,&wr,&bad_wr));
//    printf("Recv 1 posted\n");
    post_recv(id->context);
}

void build_mr(rdma_cm_id *id)
{
    connection *conn=(connection*)id->context;
    conn->msg_recv=new Msg;
    conn->msg_send=new Msg;
    conn->char_rdma_for_remote=new char[Buffersize];
    conn->char_rdma_for_local=new char[Buffersize];
    TEST_NZ(conn->mr_recv=ibv_reg_mr(s_ctx->pd,conn->msg_recv,sizeof(Msg),IBV_ACCESS_LOCAL_WRITE));
    TEST_NZ(conn->mr_send=ibv_reg_mr(s_ctx->pd,conn->msg_send,sizeof(Msg),0));
    TEST_NZ(conn->mr_rdma_for_remote=ibv_reg_mr(s_ctx->pd,conn->char_rdma_for_remote,Buffersize,
                                                IBV_ACCESS_REMOTE_READ|IBV_ACCESS_REMOTE_WRITE|
                                                        IBV_ACCESS_LOCAL_WRITE));
    TEST_NZ(conn->mr_rdma_for_local=ibv_reg_mr(s_ctx->pd,conn->char_rdma_for_local,Buffersize,
                                               IBV_ACCESS_LOCAL_WRITE));
    printf("For remote RDMA op: addr=%p rkey=%u\n",conn->mr_rdma_for_remote->addr,conn->mr_rdma_for_remote->rkey);
}

void build_qp(rdma_cm_id *id)
{
    ibv_qp_init_attr attr;
    connection *conn = (connection *) id->context;
    memset(&attr,0,sizeof(attr));
    attr.cap.max_send_wr=attr.cap.max_recv_wr=10;
    attr.cap.max_send_sge=attr.cap.max_recv_sge=1;
    attr.qp_type=IBV_QPT_RC;
    attr.send_cq=attr.recv_cq=s_ctx->cq;
    TEST_Z(rdma_create_qp(id,s_ctx->pd,&attr));//TODO :this function differs from ibv_?
//    TEST_NZ(id->qp=ibv_create_qp(s_ctx->pd,&attr));
    conn->qp=id->qp;
    printf("qp num=%u\n",id->qp->qp_num);
    printf("qp state: %u\n",conn->qp->state);
    fflush(stdout);
}

void build_s_ctx(ibv_context *pContext)
{
    if (s_ctx)
    {
        if (s_ctx->verbs!=pContext)
            die("Different verbs.");
        return;
    }
    s_ctx=new context;
    s_ctx->verbs=pContext;
    TEST_NZ(s_ctx->pd=ibv_alloc_pd(pContext));
    TEST_NZ(s_ctx->comp_channel=ibv_create_comp_channel(pContext));
    TEST_NZ(s_ctx->cq=ibv_create_cq(pContext,10, nullptr,s_ctx->comp_channel, 0));
    TEST_Z(ibv_req_notify_cq(s_ctx->cq,0));

    pthread_create(&s_ctx->pthread, nullptr,poll_cq, nullptr);
}

void *poll_cq(void *)
{
    printf("Start poll cq\n");
    ibv_wc wc;
    ibv_cq *cq;
    void *np;
    bool rdma_sent=false;
    while (1)
    {
        printf("s_ctx.verbs=%p  cc=%p\n",s_ctx->verbs,s_ctx->comp_channel);
        TEST_Z(ibv_get_cq_event(s_ctx->comp_channel,&cq, &np));//TODO:record this sentence
        ibv_ack_cq_events(cq,1);
        TEST_Z(ibv_req_notify_cq(cq,7));

        while (ibv_poll_cq(cq, 1, &wc)>0)//ibv_poll_cq return value is #returned cq
        {
            connection *conn=(connection *)wc.wr_id;
            if (wc.status!=IBV_WC_SUCCESS)
            {
                printf("wc.status= %s opcode= %d\n",ibv_wc_status_str(wc.status),wc.opcode);
                printf("qp.state=%d cq.cqe=%d\n",conn->qp->state,s_ctx->cq->cqe);
//                die("wc status not success");
            }
//            TEST_Z(ibv_req_notify_cq(s_ctx->cq,0));

            if (wc.opcode == IBV_WC_SEND)
            {
                printf("#POLL:Send successful\n");
                if (conn->send_status==connection::INIT_SEND)
                    conn->send_status=connection::MR_SENT;
                else if(conn->send_status==connection::MR_SENT)
                    conn->send_status=connection::DONE_SENT;
                else
                    die("Sent error");
            } else if (wc.opcode == IBV_WC_RECV)
            {
//                if (isClient)//------------------DEBUG----------------
//                {
//                    TEST_Z(rdma_disconnect(conn->id));
//                }
                printf("#POLL:Recv %d successful\n",conn->post_recv_cnt);
                if (conn->recv_status==connection::INIT_RECV)
                {
                    post_recv((void*)wc.wr_id);
                    conn->recv_status=connection::MR_RECV;
                    connection *conn=(connection*)wc.wr_id;
                    assert(conn->msg_recv->type==Msg::MSG_MR);
                }
                else if(conn->recv_status==connection::MR_RECV)
                {
                    conn->recv_status=connection::DONE_RECV;
                    assert(conn->msg_recv->type==Msg::MSG_DONE);
                }
                else
                    die("Recv_status error, get a recv at an wrong time");
            } else if (wc.opcode == IBV_WC_RDMA_READ||wc.opcode == IBV_WC_RDMA_WRITE)
            {
                printf("#POLL:RDMA op successful\n");
            }
            else
                die("Unknown wc");
            printf("SS=%d RS=%d\n",conn->send_status,conn->recv_status);

            if (conn->send_status==connection::MR_SENT&&conn->recv_status==connection::MR_RECV)
            {
                conn->send_status = connection::DONE_SENT;
                conn->recv_status = connection::DONE_RECV;
                if (!rdma_sent)
                {
                    printf("#POLL:Going to send rdma & done\n");
                    rdma_operation(conn);
                    send_done(conn);
                    rdma_sent=true;
                }
            }
            if (conn->send_status == connection::DONE_SENT
                && conn->recv_status == connection::DONE_RECV)
            {
                printf("#POLL:Try to Disconnected\n");
                printf("Message from peer: %s\n",conn->char_rdma_for_local);
                printf("qp.state=%d cq.cqe=%d\n",conn->qp->state,s_ctx->cq->cqe);
                if (isClient)
                {
                    TEST_Z(rdma_disconnect(conn->id));
                }
                break;
            }
        }
    }
    return nullptr;
}

void rdma_operation(connection *conn)
{
    printf("Ready to post. Remote RDMA op: addr=%p rkey=%u\n",conn->msg_recv->mr.addr,conn->msg_recv->mr.rkey);
    snprintf(conn->char_rdma_for_remote,Buffersize,"This msg is from pid %d.\n",getpid());

    ibv_send_wr *wr=new ibv_send_wr,*bad_wr;
    ibv_sge sge;
    memset(wr,0,sizeof(ibv_send_wr));
    wr->opcode=(mode==MODE::READ)?IBV_WR_RDMA_READ:IBV_WR_RDMA_WRITE;
    wr->wr_id=(uintptr_t)conn;
    wr->next= nullptr;
    wr->sg_list=&sge;
    wr->num_sge=1;
    wr->send_flags=IBV_SEND_SIGNALED;
    wr->wr.rdma.remote_addr=(uintptr_t)conn->msg_recv->mr.addr;
    wr->wr.rdma.rkey=conn->msg_recv->mr.rkey;


    sge.addr=(uintptr_t)conn->char_rdma_for_local;
    sge.lkey=conn->mr_rdma_for_local->lkey;
    sge.length=Buffersize;
    TEST_Z(ibv_post_send(conn->qp,wr,&bad_wr));
    printf("RDMA posted\n");
}

void post_recv(void *pConn)
{
    connection *conn=(connection*)pConn;
    ibv_recv_wr wr,*bad_wr;
    ibv_sge sge;
    wr.wr_id=(uintptr_t)conn;
    wr.num_sge=1;
    wr.sg_list=&sge;
    wr.next= nullptr;
    sge.length=sizeof(Msg);
    sge.lkey=conn->mr_recv->lkey;
    sge.addr=(uintptr_t)conn->msg_recv;
    TEST_Z(ibv_post_recv(conn->qp,&wr,&bad_wr));
    conn->post_recv_cnt++;
    printf("Recv %d posted\n",conn->post_recv_cnt);
}

void send_mr(void *pConn)
{
    connection *conn=(connection*)pConn;
    conn->msg_send->type=Msg::MSG_MR;
    conn->msg_send->mr=*conn->mr_rdma_for_remote;

    ibv_send_wr wr,*bad_wr;
    ibv_sge sge;
    memset(&wr,0,sizeof(wr));
    wr.opcode=IBV_WR_SEND;
    wr.num_sge=1;
    wr.sg_list=&sge;
    wr.wr_id=(uintptr_t)conn;
    wr.send_flags=IBV_SEND_SIGNALED;

//    sge.addr=(uintptr_t)conn->mr_send->addr;
//    sge.length=conn->mr_send->length;
    sge.addr=(uintptr_t)conn->msg_send;
    sge.length=sizeof(Msg);
    sge.lkey=conn->mr_send->lkey;
    TEST_Z(ibv_post_send(conn->qp,&wr,&bad_wr));
    printf("Post Send mr\n");
}

void send_done(connection *conn)
{
    conn->msg_send->type=Msg::MSG_DONE;
    memset(&conn->msg_send->mr,0,sizeof(conn->msg_send->mr));

    ibv_send_wr wr,*bad_wr;
    ibv_sge sge;
    memset(&wr,0,sizeof(wr));
    wr.opcode=IBV_WR_SEND;
    wr.num_sge=1;
    wr.sg_list=&sge;
    wr.wr_id=(uintptr_t)conn;
    wr.send_flags=IBV_SEND_SIGNALED;

//    sge.addr=(uintptr_t)conn->mr_send->addr;
//    sge.length=conn->mr_send->length;
    sge.addr=(uintptr_t)conn->msg_send;
    sge.length=sizeof(Msg);
    sge.lkey=conn->mr_send->lkey;
    TEST_Z(ibv_post_send(conn->qp,&wr,&bad_wr));
    printf("Post Send done\n");
}

void clear(void *pConn)
{
    printf("Clear resource.\n");
    connection *conn=(connection*)pConn;
    ibv_dereg_mr(conn->mr_recv);
    ibv_dereg_mr(conn->mr_send);
    ibv_dereg_mr(conn->mr_rdma_for_remote);
    ibv_dereg_mr(conn->mr_rdma_for_local);
    delete(conn->msg_send);
    delete(conn->msg_recv);
    delete[](conn->char_rdma_for_remote);
    delete[](conn->char_rdma_for_local);
    ibv_destroy_qp(conn->qp);
//    ibv_destroy_cq(s_ctx->cq);
//    ibv_dealloc_pd(s_ctx->pd);
    rdma_destroy_id(conn->id);
    delete(conn);
}

void set_mode(MODE m)
{
    mode=m;
}

void build_param(rdma_conn_param *param)
{
    memset(param, 0, sizeof(rdma_conn_param));
    param->initiator_depth = param->responder_resources = 1;//RDMA属性 TODO: what's this
    param->rnr_retry_count = 7;//7 /* infinite retry */TODO:FATEL !!!ELSE WITH RESULT IN ERROR
}

void try_disconnect(void *p)
{
    puts("Try to disconnect");
    connection *conn=(connection*)p;
    printf("Port num=%d\n",conn->id->port_num);
    rdma_disconnect(conn->id);
}
