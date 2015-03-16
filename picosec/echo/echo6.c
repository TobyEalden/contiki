/*
 * This is a small example of how to write a TCP server using
 * Contiki's protosockets. It is a simple server that accepts one line
 * of text from the TCP connection, and echoes back the first 10 bytes
 * of the string, and then closes the connection.
 *
 * The server only handles one connection at a time.
 *
 */

#include <string.h>
#include <stdio.h>

/*
 * We include "contiki-net.h" to get all network definitions and
 * declarations.
 */
#include "contiki-net.h"
#include "uip-over-mesh.h"
#include "slip.h"

/*
 * We define one protosocket since we've decided to only handle one
 * connection at a time. If we want to be able to handle more than one
 * connection at a time, each parallell connection needs its own
 * protosocket.
 */
static struct psock ps;

/*
 * We must have somewhere to put incoming data, and we use a 10 byte
 * buffer for this purpose.
 */
static char buffer[10];

static struct uip_fw_netif meshif = {UIP_FW_NETIF(172,16,0,0, 255,255,0,0, uip_over_mesh_send)};
static struct uip_fw_netif slipif = {UIP_FW_NETIF(0,0,0,0, 0,0,0,0, slip_send)};
static uint8_t is_gateway;

/*---------------------------------------------------------------------------*/
/*
 * A protosocket always requires a protothread. The protothread
 * contains the code that uses the protosocket. We define the
 * protothread here.
 */
static
PT_THREAD(handle_connection(struct psock *p))
{
  /*
   * A protosocket's protothread must start with a PSOCK_BEGIN(), with
   * the protosocket as argument.
   *
   * Remember that the same rules as for protothreads apply: do NOT
   * use local variables unless you are very sure what you are doing!
   * Local (stack) variables are not preserved when the protothread
   * blocks.
   */
  PSOCK_BEGIN(p);

  /*
   * We start by sending out a welcoming message. The message is sent
   * using the PSOCK_SEND_STR() function that sends a null-terminated
   * string.
   */
  PSOCK_SEND_STR(p, "Welcome, please type something and press return.\n");
  
  /*
   * Next, we use the PSOCK_READTO() function to read incoming data
   * from the TCP connection until we get a newline character. The
   * number of bytes that we actually keep is dependant of the length
   * of the input buffer that we use. Since we only have a 10 byte
   * buffer here (the buffer[] array), we can only remember the first
   * 10 bytes received. The rest of the line up to the newline simply
   * is discarded.
   */
  PSOCK_READTO(p, '\n');
  
  /*
   * And we send back the contents of the buffer. The PSOCK_DATALEN()
   * function provides us with the length of the data that we've
   * received. Note that this length will not be longer than the input
   * buffer we're using.
   */
  PSOCK_SEND_STR(p, "Got the following data: ");
  PSOCK_SEND(p, buffer, PSOCK_DATALEN(p));
  PSOCK_SEND_STR(p, "Good bye!\r\n");

  /*
   * We close the protosocket.
   */
  PSOCK_CLOSE(p);

  /*
   * And end the protosocket's protothread.
   */
  PSOCK_END(p);
}

static void
set_gateway(void)
{
  if(!is_gateway) {
    printf("%d.%d: making myself the IP network gateway.\n\n", rimeaddr_node_addr.u8[0], rimeaddr_node_addr.u8[1]);
    printf("IPv4 address of the gateway: %d.%d.%d.%d\n\n",  uip_ipaddr_to_quad(&uip_hostaddr));
    uip_over_mesh_set_gateway(&rimeaddr_node_addr);
    uip_over_mesh_make_announced_gateway();
    is_gateway = 1;
  }
}

/*---------------------------------------------------------------------------*/
/*
 * We declare the process.
 */
PROCESS(example_psock_server_process, "Example protosocket server");
AUTOSTART_PROCESSES(&example_psock_server_process);
/*---------------------------------------------------------------------------*/
/*
 * The definition of the process.
 */
PROCESS_THREAD(example_psock_server_process, ev, data)
{
  /*
   * The process begins here.
   */
  PROCESS_BEGIN();

  slip_arch_init(0);

  process_start(&tcpip_process, NULL);
  process_start(&uip_fw_process, NULL);
  process_start(&slip_process, NULL);

  slip_set_input_callback(set_gateway);

  uip_ipaddr_t hostaddr, netmask;
 
  uip_init();

  uip_ipaddr(&hostaddr, 172,16, rimeaddr_node_addr.u8[0],rimeaddr_node_addr.u8[1]);
  uip_ipaddr(&netmask, 255,255,0,0);
  uip_ipaddr_copy(&meshif.ipaddr, &hostaddr);

  uip_sethostaddr(&hostaddr);
  uip_setnetmask(&netmask);
  uip_over_mesh_set_net(&hostaddr, &netmask);

  uip_over_mesh_set_gateway_netif(&slipif);
  uip_fw_default(&meshif);
  uip_over_mesh_init(8);

  printf("uIP started with IP address %d.%d.%d.%d\n",  uip_ipaddr_to_quad(&hostaddr));

  /*
   * We start with setting up a listening TCP port. Note how we're
   * using the UIP_HTONS() macro to convert the port number (1010) to
   * network byte order as required by the tcp_listen() function.
   */
  tcp_listen(UIP_HTONS(12345));

  printf("listening\n");

  /*
   * We loop for ever, accepting new connections.
   */
  while(1) {

    /*
     * We wait until we get the first TCP/IP event, which probably
     * comes because someone connected to us.
     */
    PROCESS_WAIT_EVENT_UNTIL(ev == tcpip_event);

    /*
     * If a peer connected with us, we'll initialize the protosocket
     * with PSOCK_INIT().
     */
    if(uip_connected()) {
      
      /*
       * The PSOCK_INIT() function initializes the protosocket and
       * binds the input buffer to the protosocket.
       */
      PSOCK_INIT(&ps, buffer, sizeof(buffer));

      /*
       * We loop until the connection is aborted, closed, or times out.
       */
      while(!(uip_aborted() || uip_closed() || uip_timedout())) {

  /*
   * We wait until we get a TCP/IP event. Remember that we
   * always need to wait for events inside a process, to let
   * other processes run while we are waiting.
   */
  PROCESS_WAIT_EVENT_UNTIL(ev == tcpip_event);

  /*
   * Here is where the real work is taking place: we call the
   * handle_connection() protothread that we defined above. This
   * protothread uses the protosocket to receive the data that
   * we want it to.
   */
  handle_connection(&ps);
      }
    }
  }
  
  /*
   * We must always declare the end of a process.
   */
  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
