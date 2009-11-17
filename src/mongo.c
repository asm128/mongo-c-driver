/* mongo.c */

#include "mongo.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>

/* only need one of these */
static const int zero = 0;
static const int one = 1;

/* ----------------------------
   message stuff
   ------------------------------ */

static void looping_write(const int sock, const void* buf, int len){
    const char* cbuf = buf;
    while (len){
        /* TODO handle -1 */
        int sent = write(sock, cbuf, len);
        cbuf += sent;
        len -= sent;
    }
}

static void looping_read(const int sock, void* buf, int len){
    char* cbuf = buf;
    while (len){
        /* TODO handle -1 */
        int sent = read(sock, cbuf, len);
        cbuf += sent;
        len -= sent;
    }
}

void mongo_message_send(const int sock, const mongo_message* mm){
    mongo_header head; /* little endian */
    bson_little_endian32(&head.len, &mm->head.len);
    bson_little_endian32(&head.id, &mm->head.id);
    bson_little_endian32(&head.responseTo, &mm->head.responseTo);
    bson_little_endian32(&head.op, &mm->head.op);
    
    looping_write(sock, &head, sizeof(head));
    looping_write(sock, &mm->data, mm->head.len - sizeof(head));
}


char * mongo_data_append( char * start , const void * data , int len ){
    memcpy( start , data , len );
    return start + len;
}

char * mongo_data_append32( char * start , const void * data){
    bson_little_endian32( start , data );
    return start + 4;
}

char * mongo_data_append64( char * start , const void * data){
    bson_little_endian64( start , data );
    return start + 8;
}

mongo_message * mongo_message_create( int len , int id , int responseTo , int op ){
    mongo_message * mm = (mongo_message*)bson_malloc( len );

    if (!id)
        id = rand();

    /* native endian (converted on send) */
    mm->head.len = len;
    mm->head.id = id;
    mm->head.responseTo = responseTo;
    mm->head.op = op;

    return mm;
}

/* ----------------------------
   connection stuff
   ------------------------------ */

int mongo_connect( mongo_connection * conn , mongo_connection_options * options ){
    conn->connected = 0;

    if ( options ){
        memcpy( &(conn->options) , options , sizeof( mongo_connection_options ) );
    }
    else {
        strcpy( conn->options.host , "127.0.0.1" );
        conn->options.port = 27017;
    }

    /* setup */

    conn->sock = 0;

    memset( conn->sa.sin_zero , 0 , sizeof(conn->sa.sin_zero) );
    conn->sa.sin_family = AF_INET;
    conn->sa.sin_port = htons(conn->options.port);
    conn->sa.sin_addr.s_addr = inet_addr( conn->options.host );
    conn->addressSize = sizeof(conn->sa);

    /* connect */
    conn->sock = socket( AF_INET, SOCK_STREAM, 0 );
    if ( conn->sock <= 0 ){
        fprintf( stderr , "couldn't get socket errno: %d" , errno );
        return -1;
    }

    if ( connect( conn->sock , (struct sockaddr*)&conn->sa , conn->addressSize ) ){
        fprintf( stderr , "couldn' connect errno: %d\n" , errno );
        return -2;
    }

    /* options */

    /* nagle */
    setsockopt( conn->sock, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof(one) );

    /* TODO signals */


    conn->connected = 1;
    return 0;
}

void mongo_insert_batch( mongo_connection * conn , const char * ns , bson ** bsons, int count){
    int size =  16 + 4 + strlen( ns ) + 1;
    int i;
    mongo_message * mm;
    char* data;

    for(i=0; i<count; i++){
        size += bson_size(bsons[i]);
    }

    mm = mongo_message_create( size , 0 , 0 , mongo_op_insert );

    data = &mm->data;
    data = mongo_data_append32(data, &zero);
    data = mongo_data_append(data, ns, strlen(ns) + 1);

    for(i=0; i<count; i++){
        data = mongo_data_append(data, bsons[i]->data, bson_size( bsons[i] ) );
    }

    mongo_message_send(conn->sock, mm);
    free(mm);
}

void mongo_insert( mongo_connection * conn , const char * ns , bson * bson ){
    char * data;
    mongo_message * mm = mongo_message_create( 16 /* header */
                                             + 4 /* ZERO */
                                             + strlen(ns)
                                             + 1 + bson_size(bson)
                                             , 0, 0, mongo_op_insert);

    data = &mm->data;
    data = mongo_data_append32(data, &zero);
    data = mongo_data_append(data, ns, strlen(ns) + 1);
    data = mongo_data_append(data, bson->data, bson_size(bson));

    mongo_message_send(conn->sock, mm);
    free(mm);
}

void mongo_update(mongo_connection* conn, const char* ns, const bson* cond, const bson* op, int flags){
    char * data;
    mongo_message * mm = mongo_message_create( 16 /* header */
                                             + 4  /* ZERO */
                                             + strlen(ns) + 1
                                             + 4  /* flags */
                                             + bson_size(cond)
                                             + bson_size(op)
                                             , 0 , 0 , mongo_op_update );

    data = &mm->data;
    data = mongo_data_append32(data, &zero);
    data = mongo_data_append(data, ns, strlen(ns) + 1);
    data = mongo_data_append32(data, &flags);
    data = mongo_data_append(data, cond->data, bson_size(cond));
    data = mongo_data_append(data, op->data, bson_size(op));

    mongo_message_send(conn->sock, mm);
    free(mm);
}

void mongo_remove(mongo_connection* conn, const char* ns, const bson* cond){
    char * data;
    mongo_message * mm = mongo_message_create( 16 /* header */
                                             + 4  /* ZERO */
                                             + strlen(ns) + 1
                                             + 4  /* ZERO */
                                             + bson_size(cond)
                                             , 0 , 0 , mongo_op_delete );

    data = &mm->data;
    data = mongo_data_append32(data, &zero);
    data = mongo_data_append(data, ns, strlen(ns) + 1);
    data = mongo_data_append32(data, &zero);
    data = mongo_data_append(data, cond->data, bson_size(cond));

    mongo_message_send(conn->sock, mm);
    free(mm);
}

mongo_reply * mongo_read_response( mongo_connection * conn ){
    mongo_header head; /* header from network */
    mongo_reply_fields fields; /* header from network */
    mongo_reply * out; /* native endian */
    int len;

    looping_read(conn->sock, &head, sizeof(head));
    looping_read(conn->sock, &fields, sizeof(fields));

    bson_little_endian32(&len, &head.len);
    out = (mongo_reply*)bson_malloc(len);

    out->head.len = len;
    bson_little_endian32(&out->head.id, &head.id);
    bson_little_endian32(&out->head.responseTo, &head.responseTo);
    bson_little_endian32(&out->head.op, &head.op);

    bson_little_endian32(&out->fields.flag, &fields.flag);
    bson_little_endian64(&out->fields.cursorID, &fields.cursorID);
    bson_little_endian32(&out->fields.start, &fields.start);
    bson_little_endian32(&out->fields.num, &fields.num);

    looping_read(conn->sock, &out->objs, len-sizeof(head)-sizeof(fields));

    return out;
}

mongo_cursor* mongo_find(mongo_connection* conn, const char* ns, bson* query, bson* fields, int nToReturn, int nToSkip, int options){
    int sl;
    mongo_cursor * cursor;
    char * data;
    mongo_message * mm = mongo_message_create( 16 + /* header */
                                               4 + /*  options */
                                               strlen( ns ) + 1 + /* ns */
                                               4 + 4 + /* skip,return */
                                               bson_size( query ) +
                                               bson_size( fields ) ,
                                               0 , 0 , mongo_op_query );


    data = &mm->data;
    data = mongo_data_append32( data , &options );
    data = mongo_data_append( data , ns , strlen( ns ) + 1 );    
    data = mongo_data_append32( data , &nToSkip );
    data = mongo_data_append32( data , &nToReturn );
    data = mongo_data_append( data , query->data , bson_size( query ) );    
    if ( fields )
        data = mongo_data_append( data , fields->data , bson_size( fields ) );    
    
    bson_fatal_msg( (data == ((char*)mm) + mm->head.len), "query building fail!" );

    mongo_message_send( conn->sock , mm );
    free(mm);

    cursor = (mongo_cursor*)bson_malloc(sizeof(mongo_cursor));

    cursor->mm = mongo_read_response(conn);
    if (!cursor->mm){
        free(cursor);
        return 0;
    }

    sl = strlen(ns)+1;
    cursor->ns = bson_malloc(sl);
    if (!cursor->ns){
        free(cursor->mm);
        free(cursor);
        return 0;
    }
    memcpy((void*)cursor->ns, ns, sl); /* cast needed to silence GCC warning */
    cursor->conn = conn;
    cursor->current.data = NULL;
    return cursor;
}

bson_bool_t mongo_find_one(mongo_connection* conn, const char* ns, bson* query, bson* fields, bson* out){
    mongo_cursor* cursor = mongo_find(conn, ns, query, fields, 1, 0, 0);

    if (cursor && mongo_cursor_next(cursor)){
        bson_copy(out, &cursor->current);
        mongo_cursor_destroy(cursor);
        return 1;
    }else{
        mongo_cursor_destroy(cursor);
        return 0;
    }
}

bson_bool_t mongo_disconnect( mongo_connection * conn ){
    if ( ! conn->connected )
        return 1;

    close( conn->sock );
    
    conn->sock = 0;
    conn->connected = 0;
    
    return 0;
}

bson_bool_t mongo_destory( mongo_connection * conn ){
    return mongo_disconnect( conn );
}

bson_bool_t mongo_cursor_get_more(mongo_cursor* cursor){
    if (cursor->mm && cursor->mm->fields.cursorID){
        char* data;
        int sl = strlen(cursor->ns)+1;
        mongo_message * mm = mongo_message_create(16 /*header*/
                                                 +4 /*ZERO*/
                                                 +sl
                                                 +4 /*numToReturn*/
                                                 +8 /*cursorID*/
                                                 , 0, 0, mongo_op_get_more);
        data = &mm->data;
        data = mongo_data_append32(data, &zero);
        data = mongo_data_append(data, cursor->ns, sl);
        data = mongo_data_append32(data, &zero);
        data = mongo_data_append64(data, &cursor->mm->fields.cursorID);
        mongo_message_send(cursor->conn->sock, mm);
        free(mm);

        free(cursor->mm);

        cursor->mm = mongo_read_response(cursor->conn);
        return cursor->mm && cursor->mm->fields.num;
    } else{
        return 0;
    }
}

bson_bool_t mongo_cursor_next(mongo_cursor* cursor){
    char* bson_addr;

    /* no data */
    if (!cursor->mm || cursor->mm->fields.num == 0)
        return 0;

    /* first */
    if (cursor->current.data == NULL){
        bson_init(&cursor->current, &cursor->mm->objs, 0);
        return 1;
    }

    bson_addr = cursor->current.data + bson_size(&cursor->current);
    if (bson_addr >= ((char*)cursor->mm + cursor->mm->head.len)){
        if (!mongo_cursor_get_more(cursor))
            return 0;
        bson_init(&cursor->current, &cursor->mm->objs, 0);
    } else {
        bson_init(&cursor->current, bson_addr, 0);
    }

    return 1;
}

void mongo_cursor_destroy(mongo_cursor* cursor){
    if (!cursor) return;

    if (cursor->mm && cursor->mm->fields.cursorID){
        mongo_message * mm = mongo_message_create(16 /*header*/
                                                 +4 /*ZERO*/
                                                 +4 /*numCursors*/
                                                 +8 /*cursorID*/
                                                 , 0, 0, mongo_op_kill_cursors);
        char* data = &mm->data;
        data = mongo_data_append32(data, &zero);
        data = mongo_data_append32(data, &one);
        data = mongo_data_append64(data, &cursor->mm->fields.cursorID);
        
        mongo_message_send(cursor->conn->sock, mm);
        free(mm);
    }
        
    free(cursor->mm);
    free((void*)cursor->ns);
    free(cursor);
}

bson_bool_t mongo_run_command(mongo_connection * conn, const char * db, bson * command, bson * out){
    bson fields;
    int sl = strlen(db);
    char* ns = bson_malloc(sl + 5 + 1); /* ".$cmd" + nul */
    bson_bool_t success;

    strcpy(ns, db);
    strcpy(ns+sl, ".$cmd");

    success = mongo_find_one(conn, ns, command, bson_empty(&fields), out);
    free(ns);
    return success;
}

bson_bool_t mongo_cmd_drop_db(mongo_connection * conn, const char * db){
    bson out;
    bson cmd;
    bson_buffer bb;
    bson_bool_t success = 0;

    bson_buffer_init(&bb);
    bson_append_int(&bb, "dropDatabase", 1);
    bson_from_buffer(&cmd, &bb);

    if(mongo_run_command(conn, db, &cmd, &out)){
        bson_iterator it;
        bson_iterator_init(&it, out.data);
        while(bson_iterator_next(&it)){
            if (strcmp("ok", bson_iterator_key(&it)) != 0)
                continue;
            success = bson_iterator_bool(&it);
            break;
        }
    }
    
    bson_destroy(&cmd);
    bson_destroy(&out);
    return success;
}

bson_bool_t mongo_cmd_drop_collection(mongo_connection * conn, const char * db, const char * collection, bson * realout){
    bson out;
    bson cmd;
    bson_buffer bb;
    bson_bool_t success = 0;

    bson_buffer_init(&bb);
    bson_append_string(&bb, "drop", collection);
    bson_from_buffer(&cmd, &bb);

    if(mongo_run_command(conn, db, &cmd, &out)){
        bson_iterator it;
        bson_iterator_init(&it, out.data);
        while(bson_iterator_next(&it)){
            if (strcmp("ok", bson_iterator_key(&it)) != 0)
                continue;
            success = bson_iterator_bool(&it);
            break;
        }
    }
    
    bson_destroy(&cmd);

    if(realout)
        *realout = out; /* transfer of ownership */
    else
        bson_destroy(&out);

    return success;
}

void mongo_cmd_reset_error(mongo_connection * conn, const char * db){
    bson cmd;
    bson_buffer bb;

    bson_buffer_init(&bb);
    bson_append_int(&bb, "reseterror", 1);
    bson_from_buffer(&cmd, &bb);

    mongo_run_command(conn, db, &cmd, NULL);
    
    bson_destroy(&cmd);
}

static bson_bool_t mongo_cmd_get_error_helper(mongo_connection * conn, const char * db, bson * realout, const char * cmdtype){
    bson out;
    bson cmd;
    bson_buffer bb;
    bson_bool_t haserror = 1;

    bson_buffer_init(&bb);
    bson_append_int(&bb, cmdtype, 1);
    bson_from_buffer(&cmd, &bb);

    if(mongo_run_command(conn, db, &cmd, &out)){
        bson_iterator it;
        bson_iterator_init(&it, out.data);
        while(bson_iterator_next(&it)){
            if (strcmp("err", bson_iterator_key(&it)) != 0)
                continue;
            haserror = (bson_iterator_type(&it) != bson_null);
            break;
        }
    }
    
    bson_destroy(&cmd);

    if(realout)
        *realout = out; /* transfer of ownership */
    else
        bson_destroy(&out);

    return haserror;
}

bson_bool_t mongo_cmd_get_prev_error(mongo_connection * conn, const char * db, bson * out){
    return mongo_cmd_get_error_helper(conn, db, out, "getpreverror");
}
bson_bool_t mongo_cmd_get_last_error(mongo_connection * conn, const char * db, bson * out){
    return mongo_cmd_get_error_helper(conn, db, out, "getlasterror");
}
