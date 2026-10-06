            else if (WS_HTTP_REDIRECT(result) ||
                     WS_HTTP_REDIRECT(esp_transport_ws_get_upgrade_request_status(client->transport))) {
                // Location cannot authorize a new Rodak trust origin or connection address.
                // Some 3xx codes can return transport success; inspect the HTTP status too.
                client->error_handle.esp_ws_handshake_status_code = WS_HTTP_REDIRECT(result)
                    ? result : esp_transport_ws_get_upgrade_request_status(client->transport);
                client->error_handle.error_type = WEBSOCKET_ERROR_TYPE_HANDSHAKE;
                esp_websocket_client_error(client, "WebSocket HTTP redirect refused (status %d)",
                                           client->error_handle.esp_ws_handshake_status_code);
                esp_websocket_client_abort_connection(client, WEBSOCKET_ERROR_TYPE_HANDSHAKE);
                break;
            }
