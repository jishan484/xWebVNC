#ifndef SERVER_CLIPBOARD_H
#define SERVER_CLIPBOARD_H

#include "selection.h"
#include "windowstr.h"

/* Flag: server currently owns clipboard */
extern Bool _server_owned_clipboard;

/* Set clipboard data */
void ServerSetClipboard(const char *data,
                        int len);

void ServerForceClearClientClipboard_(void);

/* Clear clipboard */
void ServerClearClipboard(void);

/* Handle ConvertSelection */
int _server_clipboard(ClientPtr client,
                      xConvertSelectionReq *stuff);

#endif
