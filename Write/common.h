//
// Created by eric on 18-8-20.
//

#ifndef RDMA_CM_COMMON_H
#define RDMA_CM_COMMON_H

#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <cstring>
#include <unistd.h>
#include <netdb.h>
#define TEST_Z(x) do { if ( (x)) die("error: " #x " failed (returned non-zero)." ); } while (0)
#define TEST_NZ(x)  do { if (!(x)) die("error: " #x " failed (returned zero/null)."); } while (0)

const int Buffersize=1024;
extern bool isClient;

enum class MODE{
    READ,WRITE
};

void send_mr(void *conn);//TODO 这个要放在哪里？
void build_id(rdma_cm_id *id);
void clear(void *pConn);
void set_mode(MODE m);
void build_param(rdma_conn_param *param);

inline void die(const char *reason)
{
    fprintf(stderr, "DIE: %s\n", reason);
    exit(EXIT_FAILURE);
}

inline void print_usage()
{
    printf("... <IP> <Port> <R/W>");
}

void try_disconnect(void *p);
#endif //RDMA_CM_COMMON_H
