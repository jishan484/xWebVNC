#include <security/pam_appl.h>
#include <security/pam_misc.h>
#include "webvnc.h"
#include <pwd.h>

int isLoginRequired = 0;

/* Conversation function: supplies the password to PAM */
static int pam_conversation(int num_msg,
                            const struct pam_message **msg,
                            struct pam_response **resp,
                            void *appdata_ptr) {
    if (num_msg <= 0) return PAM_CONV_ERR;

    struct pam_response *reply = calloc(num_msg, sizeof(struct pam_response));
    if (reply == NULL) return PAM_CONV_ERR;

    const char *password = (const char *)appdata_ptr;

    for (int i = 0; i < num_msg; i++) {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF ||
            msg[i]->msg_style == PAM_PROMPT_ECHO_ON) {
            reply[i].resp = strdup(password);
            reply[i].resp_retcode = 0;
        } else {
            reply[i].resp = NULL;
            reply[i].resp_retcode = 0;
        }
    }

    *resp = reply;
    return PAM_SUCCESS;
}

/* Function with exact signature */
int check_password(const char *user, const char *password) {
    if(!isLoginRequired) return 1;
    /* Step 1: Verify current process user matches `user` */
    uid_t uid = geteuid();  /* effective UID of running process */
    struct passwd *pw = getpwuid(uid);
    if (pw) {
        //if current user resolved , then match else No big deal
        if (strcmp(pw->pw_name, user) != 0) {
            return 0; /* mismatch between running user and provided username */
        }
    }

    /* Step 2: Authenticate with PAM */
    pam_handle_t *pamh = NULL;
    struct pam_conv conv = { pam_conversation, (void *)password };
    int retval;

    retval = pam_start("login", user, &conv, &pamh);
    if (retval != PAM_SUCCESS) {
        return 0;
    }

    retval = pam_authenticate(pamh, 0);
    if (retval != PAM_SUCCESS) {
        pam_end(pamh, retval);
        return 0;
    }

    retval = pam_acct_mgmt(pamh, 0);
    if (retval != PAM_SUCCESS) {
        pam_end(pamh, retval);
        return 0;
    }

    pam_end(pamh, PAM_SUCCESS);
    return 1;  /* success */
}
