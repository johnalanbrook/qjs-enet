// enet_qjs.c
#include "quickjs.h"
#include <enet/enet.h>
#include <stdio.h>
#include <string.h>

#define countof(a) (sizeof(a)/sizeof(*(a)))

static JSClassID enet_host_id;
static JSClassID enet_peer_class_id;

static void js_enet_host_finalizer(JSRuntime *rt, JSValue val) {
    ENetHost *host = JS_GetOpaque(val, enet_host_id);
    if (host) {
        enet_host_destroy(host);
    }
}

static void js_enet_peer_finalizer(JSRuntime *rt, JSValue val) {
    ENetPeer *peer = JS_GetOpaque(val, enet_peer_class_id);
    if (peer) {
        // No explicit cleanup needed for ENetPeer
    }
}

static JSValue js_enet_initialize(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (enet_initialize() != 0) {
        return JS_ThrowInternalError(ctx, "An error occurred while initializing ENet.");
    }
    return JS_UNDEFINED;
}

static JSValue js_enet_deinitialize(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    enet_deinitialize();
    return JS_UNDEFINED;
}

static JSValue js_enet_host_create(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ENetHost *host;
    ENetAddress address;
    JSValue obj;
    if (argc < 1) {
        host = enet_host_create(NULL, 32, 2, 0, 0);
        goto RET;
    }

    const char *address_str = JS_ToCString(ctx, argv[0]);
    if (!address_str)
        return JS_EXCEPTION;

    char ip[64];
    int port;

    if (sscanf(address_str, "%63[^:]:%d", ip, &port) != 2) {
        JS_FreeCString(ctx, address_str);
        return JS_ThrowTypeError(ctx, "Invalid address format. Expected format: 'ip:port'");
    }

    JS_FreeCString(ctx, address_str);

    int err;
    if ((err = enet_address_set_host_ip(&address, ip)) != 0) {
        return JS_ThrowInternalError(ctx, "Failed to set host IP from %s. Error %d.", ip, err);
    }
    address.port = port;

    host = enet_host_create(&address, 32, 2, 0, 0); // server host with max 32 clients
    if (!host) {
        return JS_ThrowInternalError(ctx, "Failed to create ENet host.");
    }

    RET:
    obj = JS_NewObjectClass(ctx, enet_host_id);
    if (JS_IsException(obj)) {
        enet_host_destroy(host);
        return obj;
    }

    JS_SetOpaque(obj, host);
    return obj;
}

static JSValue js_enet_host_service(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ENetHost *host = JS_GetOpaque(this_val, enet_host_id);
    if (!host)
        return JS_EXCEPTION;

    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "Expected a callback function as the first argument");
    }

    JSValue callback = argv[0];
    JS_DupValue(ctx, callback);

    ENetEvent event;
    int timeout = 1000; // 1 second timeout by default

    if (argc > 1) {
        JS_ToInt32(ctx, &timeout, argv[1]);
    }

    while (enet_host_service(host, &event, timeout) > 0) {
        JSValue event_obj = JS_NewObject(ctx);
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                JS_SetPropertyStr(ctx, event_obj, "type", JS_NewString(ctx, "connect"));
                JSValue peer_obj = JS_NewObjectClass(ctx, enet_peer_class_id);
                if (JS_IsException(peer_obj))
                    return peer_obj;
                JS_SetOpaque(peer_obj, event.peer);
                JS_SetPropertyStr(ctx, event_obj, "peer", peer_obj);
                break;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                JS_SetPropertyStr(ctx, event_obj, "type", JS_NewString(ctx, "receive"));
                JS_SetPropertyStr(ctx, event_obj, "channelID", JS_NewInt32(ctx, event.channelID));
                JSValue packet_data = JS_ParseJSON(ctx, (const char *)event.packet->data, event.packet->dataLength, "packet");
                if (JS_IsException(packet_data)) {
                    packet_data = JS_NULL;
                }
                JS_SetPropertyStr(ctx, event_obj, "data", packet_data);
                enet_packet_destroy(event.packet);
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                JS_SetPropertyStr(ctx, event_obj, "type", JS_NewString(ctx, "disconnect"));
                break;
            }
        }
        JS_Call(ctx, callback, JS_UNDEFINED, 1, &event_obj);
        JS_FreeValue(ctx, event_obj);
    }

    JS_FreeValue(ctx, callback);
    return JS_UNDEFINED;
}                

static JSValue js_enet_host_connect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ENetHost *host = JS_GetOpaque(this_val, enet_host_id);
    if (!host)
        return JS_EXCEPTION;

    if (argc < 2)
        return JS_ThrowTypeError(ctx, "Expected at least 2 arguments (host, port)");

    const char *host_name = JS_ToCString(ctx, argv[0]);
    int port;
    JS_ToInt32(ctx, &port, argv[1]);

    ENetAddress address;
    enet_address_set_host(&address, host_name);
    address.port = port;
    JS_FreeCString(ctx, host_name);

    ENetPeer *peer = enet_host_connect(host, &address, 2, 0);
    if (!peer)
        return JS_ThrowInternalError(ctx, "Failed to initiate connection.");

    JSValue peer_obj = JS_NewObjectClass(ctx, enet_peer_class_id);
    if (JS_IsException(peer_obj))
        return peer_obj;
    JS_SetOpaque(peer_obj, peer);
    return peer_obj;
}

static JSValue js_enet_host_flush(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ENetHost *host = JS_GetOpaque(this_val, enet_host_id);
    if (!host)
        return JS_EXCEPTION;

    enet_host_flush(host);
    return JS_UNDEFINED;
}

static JSValue js_enet_host_broadcast(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ENetHost *host = JS_GetOpaque(this_val, enet_host_id);
    if (!host)
        return JS_EXCEPTION;

    if (argc < 1)
        return JS_ThrowTypeError(ctx, "Expected at least 1 argument (data)");

    size_t data_len;
    const char *data = JS_ToCStringLen(ctx, &data_len, argv[0]);
    if (!data)
        return JS_EXCEPTION;

    ENetPacket *packet = enet_packet_create(data, data_len, ENET_PACKET_FLAG_RELIABLE);
    JS_FreeCString(ctx, data);

    enet_host_broadcast(host, 0, packet);
    return JS_UNDEFINED;
}

static JSValue js_enet_peer_disconnect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ENetPeer *peer = JS_GetOpaque(this_val, enet_peer_class_id);
    if (!peer)
        return JS_EXCEPTION;

    enet_peer_disconnect(peer, 0);
    return JS_UNDEFINED;
}

static JSValue js_enet_peer_send(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ENetPeer *peer = JS_GetOpaque(this_val, enet_peer_class_id);
    if (!peer)
        return JS_EXCEPTION;

    if (argc < 1 || !JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "Expected at least 1 argument (object)");

    JSValue json_data = JS_JSONStringify(ctx, argv[0], JS_NULL, JS_NULL);
    if (JS_IsException(json_data))
        return JS_EXCEPTION;

    const char *data = JS_ToCString(ctx, json_data);
    if (!data)
        return JS_EXCEPTION;

    ENetPacket *packet = enet_packet_create(data, strlen(data) + 1, ENET_PACKET_FLAG_RELIABLE);
    JS_FreeCString(ctx, data);
    JS_FreeValue(ctx, json_data);

    if (enet_peer_send(peer, 0, packet) < 0) {
        return JS_ThrowInternalError(ctx, "Failed to send packet.");
    }

    return JS_UNDEFINED;
}

static JSValue js_enet_peer_disconnect_now(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ENetPeer *peer = JS_GetOpaque(this_val, enet_peer_class_id);
    if (!peer)
        return JS_EXCEPTION;

    enet_peer_disconnect_now(peer, 0);
    return JS_UNDEFINED;
}

static JSValue js_enet_peer_disconnect_later(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ENetPeer *peer = JS_GetOpaque(this_val, enet_peer_class_id);
    if (!peer)
        return JS_EXCEPTION;

    enet_peer_disconnect_later(peer, 0);
    return JS_UNDEFINED;
}

static JSValue js_enet_peer_reset(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ENetPeer *peer = JS_GetOpaque(this_val, enet_peer_class_id);
    if (!peer)
        return JS_EXCEPTION;

    enet_peer_reset(peer);
    return JS_UNDEFINED;
}

static JSValue js_enet_peer_ping(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ENetPeer *peer = JS_GetOpaque(this_val, enet_peer_class_id);
    if (!peer)
        return JS_EXCEPTION;

    enet_peer_ping(peer);
    return JS_UNDEFINED;
}

static JSValue js_enet_peer_throttle_configure(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ENetPeer *peer = JS_GetOpaque(this_val, enet_peer_class_id);
    if (!peer)
        return JS_EXCEPTION;

    int interval, acceleration, deceleration;
    if (argc < 3 || JS_ToInt32(ctx, &interval, argv[0]) || JS_ToInt32(ctx, &acceleration, argv[1]) || JS_ToInt32(ctx, &deceleration, argv[2]))
        return JS_ThrowTypeError(ctx, "Expected 3 integer arguments (interval, acceleration, deceleration)");

    enet_peer_throttle_configure(peer, interval, acceleration, deceleration);
    return JS_UNDEFINED;
}

static JSValue js_enet_peer_timeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    ENetPeer *peer = JS_GetOpaque(this_val, enet_peer_class_id);
    if (!peer)
        return JS_EXCEPTION;

    int timeout_limit, timeout_min, timeout_max;
    if (argc < 3 || JS_ToInt32(ctx, &timeout_limit, argv[0]) || JS_ToInt32(ctx, &timeout_min, argv[1]) || JS_ToInt32(ctx, &timeout_max, argv[2]))
        return JS_ThrowTypeError(ctx, "Expected 3 integer arguments (timeout_limit, timeout_min, timeout_max)");

    enet_peer_timeout(peer, timeout_limit, timeout_min, timeout_max);
    return JS_UNDEFINED;
}

static JSClassDef enet_host = {
    "ENetHost",
    .finalizer = js_enet_host_finalizer,
};

static JSClassDef enet_peer_class = {
    "ENetPeer",
    .finalizer = js_enet_peer_finalizer,
};

static const JSCFunctionListEntry js_enet_funcs[] = {
    JS_CFUNC_DEF("initialize", 0, js_enet_initialize),
    JS_CFUNC_DEF("deinitialize", 0, js_enet_deinitialize),
    JS_CFUNC_DEF("create_host", 1, js_enet_host_create),
};

static const JSCFunctionListEntry js_enet_host_funcs[] = {
    JS_CFUNC_DEF("service", 1, js_enet_host_service),
    JS_CFUNC_DEF("connect", 2, js_enet_host_connect),
    JS_CFUNC_DEF("flush", 0, js_enet_host_flush),
    JS_CFUNC_DEF("broadcast", 1, js_enet_host_broadcast),
};

static const JSCFunctionListEntry js_enet_peer_funcs[] = {
    JS_CFUNC_DEF("send", 1, js_enet_peer_send),
    JS_CFUNC_DEF("disconnect", 0, js_enet_peer_disconnect),
    JS_CFUNC_DEF("disconnect_now", 0, js_enet_peer_disconnect_now),
    JS_CFUNC_DEF("disconnect_later", 0, js_enet_peer_disconnect_later),
    JS_CFUNC_DEF("reset", 0, js_enet_peer_reset),
    JS_CFUNC_DEF("ping", 0, js_enet_peer_ping),
    JS_CFUNC_DEF("throttle_configure", 3, js_enet_peer_throttle_configure),
    JS_CFUNC_DEF("timeout", 3, js_enet_peer_timeout),
};

static int js_enet_init(JSContext *ctx, JSModuleDef *m) {
    JS_NewClassID(&enet_host_id);
    JS_NewClass(JS_GetRuntime(ctx), enet_host_id, &enet_host);
    JSValue host_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, host_proto, js_enet_host_funcs, countof(js_enet_host_funcs));
    JS_SetClassProto(ctx, enet_host_id, host_proto);

    JS_NewClassID(&enet_peer_class_id);
    JS_NewClass(JS_GetRuntime(ctx), enet_peer_class_id, &enet_peer_class);
    JSValue peer_proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, peer_proto, js_enet_peer_funcs, countof(js_enet_peer_funcs));
    JS_SetClassProto(ctx, enet_peer_class_id, peer_proto);

    return JS_SetModuleExportList(ctx, m, js_enet_funcs, countof(js_enet_funcs));
}

#ifdef JS_SHARED_LIBRARY
#define JS_INIT_MODULE js_init_module
#else
#define JS_INIT_MODULE js_init_module_enet
#endif

JSModuleDef *JS_INIT_MODULE(JSContext *ctx, const char *module_name) {
    JSModuleDef *m = JS_NewCModule(ctx, module_name, js_enet_init);
    if (!m)
        return NULL;
    JS_AddModuleExportList(ctx, m, js_enet_funcs, countof(js_enet_funcs));
    return m;
}
