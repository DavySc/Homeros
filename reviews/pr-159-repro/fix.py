# the reviewer's suggested change to close_notify(): no session tickets, and the
# reason and the close_notify corked into one segment, so no fixed sleep
import sys
s=open(sys.argv[1]).read()
def rep(old,new):
    global s
    assert s.count(old)==1, old
    s=s.replace(old,new)
rep('#include <netinet/in.h>\n','#include <netinet/in.h>\n#include <netinet/tcp.h>\n')
rep('\tSSL *sv = SSL_new(ctx);\n','\tSSL_CTX_set_num_tickets(ctx, 0);   /* nothing of the server\'s is on its way when it corks */\n\tSSL *sv = SSL_new(ctx);\n')
rep('''	CHECK(SSL_is_init_finished(sv) && SSL_write(sv, "reason", 6) == 6 && SSL_shutdown(sv) >= 0);
	CHECK(poll(&(struct pollfd){c.fd, POLLIN, 0}, 1, 1000) == 1 && !nanosleep(&(struct timespec){0, 50000000}, NULL));
''','''	/* corked: both records leave as one segment, so they arrive together */
	CHECK(SSL_is_init_finished(sv) && !setsockopt(f, IPPROTO_TCP, TCP_CORK, &(int){1}, sizeof(int)) && SSL_write(sv, "reason", 6) == 6 && SSL_shutdown(sv) >= 0
		&& !setsockopt(f, IPPROTO_TCP, TCP_CORK, &(int){0}, sizeof(int)));
	CHECK(poll(&(struct pollfd){c.fd, POLLIN, 0}, 1, 1000) == 1);
''')
open(sys.argv[2],'w').write(s)
