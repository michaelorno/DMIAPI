//	dmiapi.c 	28082021/MOE
//	Build: cc dmiapi.c -o dmiapi -lssl -lcrypto -ljson-c -lm
//      https://github.com/michaelorno/DMIOV.git
//
//	Call: ./dmiapi <configurationfile>
//	
//	Monitor DMI metObs/oceanObs/lightning API
//		See https://www.dmi.dk/friedata/guides-til-frie-data/
//
//	Function:
//	Issues a "GET"-request for each API and waits [FREQ]
//	Measures response time in milliseconds
//	Calculates average response time for each 10, 100, 1000 request & high/low (resets at 1000 requests)
//	Generate [WWW-PATH]/index.html for output
//	If [SILENT]=1 shows a monitor on tty
//
//	Parameters in configurationfile (*)
//		[USERID] identifier (string without whitespaces)
//      	[IPHOST] ip-address for gateway (IPv4 xxx.xxx.xxx.xxx)
//      	[FREQ] wait n seconds between issue of request-triplet (int)
//      	[WWW-PATH] path for index.html-file (string)
//      	[METOBSKEY] api-key (string) - obtain key at dmi.dk
//      	[OCEANOBSKEY] api-key (string) - obtain key at dmi.dk
//      	[LIGHTOBSKEY] api-key (string) - obtain key at dmi.dk
//      	[CLIMATEKEY] api-key (string) - obtain key at dmi.dk
//      	[METOBSAPI_THRESHOLD_WARNING] threshold for issue of warning i syslog in ms (int)
//      	[METOBSAPI_THRESHOLD_ERROR] threshold for issue of error in syslog in  ms  (int)
//      	[OCEANOBSAPI_THRESHOLD_WARNING] threshold for issue of warning i syslog in ms (int)
//      	[OCEANOBSAPI_THRESHOLD_ERROR] threshold for issue of error in syslog in  ms  (int)
//      	[LIGHTOBSAPI_THRESHOLD_WARNING] threshold for issue of warning i syslog in ms (int)
//      	[LIGHTOBSAPI_THRESHOLD_ERROR] threshold for issue of error in syslog in  ms  (int)
//      	[CLIMATEOBSAPI_THRESHOLD_WARNING] threshold for issue of warning i syslog in ms (int)
//      	[CLIMATEOBSAPI_THRESHOLD_ERROR] threshold for issue of error in syslog in  ms  (int)
//      	[SILENT] 0|1  (0=slient, 1=console output))
//      	(*) Remark: [PARAMETER] and value must be separated by a white space
//
//	Dokumentation: dmiapi.txt
//
//	Versionshistorie:
//		0.91 Initial version
//		0.92 All 3 API's added
//		0.93 Syslog implemented
//		0.94 Code tidy-up and publish on github
//		0.95 Expanded http returncode handling & new lightObs query
//		0.96 Fixed no-response from network problem
//		0.97 Fixed no-response from network problem
//		0.98 Fixed minor issue, colormonitor & upgraded til metObs v2
//		0.99 SSL/TLS, JSON lib & climateObs
//		1.00 Individual thresholds for each API
//	To-do:
//		match on-line with gravetee.io translog

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <sys/socket.h>
#include <resolv.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>

// SSL
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

// JSON-C
#include <json-c/json.h>

#define	TCPIPDEBUG 0		// if !=0 then debugmsg to tty
#define	HTTPLOGGING 0		// if !=0 then output http send/receive on tty
#define VERSION "1.00"
#define MAX_BUF 5000
#define NUM_OF_APIS 3		// Counting from 0 = 1 API, 3 = 4 APIs

#define HTML_GREEN  "<span style=\"color:green\">"
#define HTML_YELLOW "<span style=\"color:orange\">"
#define HTML_RED    "<span style=\"color:red\">"
#define HTML_END    "</span>"

// File definitions
FILE *http_debug_file;	// HTTP debugging
FILE *http_out;		// Write index.html-file
FILE *syslog_out;	// Local copy of messages to syslog
FILE *statlog_out;	// Statistics-log
FILE *translog_out;	// Transaction-log
FILE *config_file;	// Configuration-file

// Measure mem
struct rusage r_usage;

// Variables for TCPIP
int online, server = 0;

// Variables for SSL
BIO *certbio = NULL;
BIO *outbio = NULL;
X509 *cert = NULL;
X509_NAME *certname = NULL;
const SSL_METHOD *method;
SSL_CTX *ctx;
SSL *ssl;

// Variables for timekeeping
time_t start_time;
time_t current_time;
time_t file_current_time;
struct tm *today;
char start_c_time_string[30] = {0};;
struct timeval t0;
struct timeval t1;
char trans_dato[80] = {0};

// Outputscreen
struct screen_array{
   char line[132];
   } screen[32];

// Statistics
struct data_record{
   char data[45];
   } observation[NUM_OF_APIS + 1];

struct http_resp_record{
   int http_204;
   int http_other;
   } http_resp[NUM_OF_APIS + 1];

struct measure_record{
   int requests;
   float elapsed;
   float elapsed_low;
   float elapsed_high;
   float elapsed_gns10;
   float elapsed_gns100;
   float elapsed_gns1000;
   float elapsed_sum10;
   float elapsed_sum100;
   float elapsed_sum1000;
   char  elapsed_html_color[35];
   char  elapsed_low_html_color[35];
   char  elapsed_high_html_color[35];
   char  elapsed_gns10_html_color[35];
   char  elapsed_gns100_html_color[35];
   char  elapsed_gns1000_html_color[35];
   int   last_returncode;
   char  last_returncode_html_color[35];
   } mea[NUM_OF_APIS + 1];

// Locations [0]-[16]
int stations_count = 0;
struct maalestation{ 	// metObs
   char* kode;
   char* navn;
   } stations_liste[]={
  "06041", "Skagen fyr      ",
  "06079", "Anholt havn     ",
  "06081", "Blaavandshuk fyr",
  "06183", "Drogden fyr     ",
  "06193", "Hammerodde fyr  ",
  "06169", "Gniben          ",
  "06119", "Kegnaes fyr     ",
  "06188", "Sjaelsmark      ",
  "06074", "Aarhus syd      ",
  "06184", "DMI             ",
  "06149", "Gedser          ",
  "06096", "Roemoe/Juvre    ",
  "06168", "Nakkehoved fyr  ",
  "06068", "Isenvad         ",
  "04320", "Danmarkshavn    ",
  "04250", "Nuuk            ",
  "04220", "Aasiaat         ",
};

struct maalestation	// oceanObs
   kyst_stations_liste[]={
  "28548", "Bagenkop havn   ",
  "27084", "Ballen havn     ",
  "30357", "Drogden fyr     ",
  "25149", "Esbjerg havn    ",
  "20101", "Frederikshavn I ",
  "31616", "Gedser havn I   ",
  "29002", "Havnebyen/Sj.od.",
  "29038", "HolbÃ¦k havn I   ",
  "29393", "KorsÃ¸r havn I   ",
  "30336", "Kbh. havn       ",
  "30407", "Roskilde havn I ",
  "31573", "Roedbyhavn I    ",
  "32048", "Tejn            ",
  "30202", "Vedbaek I       ",
  "22331", "Aarhus havn I   ",
  "26359", "Vidaa/Hoejer    ",
  "30017", "Hornbaek        ",
};

// Parametre fra konfigurationsfil
char config_filename[80];
char userid[80];
char key_metobs[80];
char key_oceanobs[80];
char key_lightobs[80];
char key_climateobs[80];
char iphost[80];
char freq[80];
char wwwpath[80];
char silent[80];
struct thresholds{
   char trs_warning[80];
   char trs_error[80];
   } th[NUM_OF_APIS + 1];
   
// Function prototypes
// TCPIP
int create_socket(char url_str[], BIO *out);
int init_com();
void close_com();
int log_ssl();
long unsigned int get_length(char* buffer);

// Logs
void write_syslog(const char* msg, int pri);
void write_statlog(char* trans_type, char* trans_date, double trans_tid, double low, double high);
void write_translog(char* trans_date, int api_id, int http_code, char* trans_id, double trans_tid);
void http_log(char* msg1, char* msg2);

// Output
void view_console();
void html_output();
void compute_colors();

// Init & and functions
int goodbye(int status_code);
void read_config(char* config_filename);

// Misc.
float timedifference_msec(struct timeval t0, struct timeval t1);

// API functions
int api_request(char* api, char* station_id);
int decode_data(int api, char* server_reply);

int main(int argc, char *argv[]){
   int g10, g100, g1000, x;
   char c, syslog_txt[80];

   http_debug_file = fopen ("dmiapi_http.log", "w");
   write_syslog("Monitor started", 0);

   // Read configuration parameters
   strcpy(config_filename,argv[1]); // filename
   read_config(config_filename);

   // Initialize SSL/TLS comm
   OpenSSL_add_all_algorithms();
   ERR_load_BIO_strings();
   SSL_load_error_strings();

   // Start time
   start_time = time(NULL);
   if (start_time == ((time_t)-1)) {
       write_syslog("Failure to obtain the current time", 3);
       goodbye(3);
       }
   strcpy(start_c_time_string, ctime(&start_time));
   start_c_time_string[strlen(start_c_time_string)-1]=0;

   // Initialize
   for (x = 0; x <= NUM_OF_APIS; x++){
      mea[x].requests = 0;
      mea[x].elapsed = 0;
      mea[x].elapsed_low = 1000;
      mea[x].elapsed_high = 0;
      mea[x].elapsed_gns10 = 0;
      mea[x].elapsed_gns100 = 0;
      mea[x].elapsed_gns1000 = 0;
      mea[x].elapsed_sum10 = 0;
      mea[x].elapsed_sum100 = 0;
      mea[x].elapsed_sum1000 = 0;
      http_resp[x].http_204 = 0;
      http_resp[x].http_other = 0;
      }
   g10 = g100 = g1000 = 1;


   while(1){
      // Do #NUM_OF_APIS requests
      for (x = 0; x <= 3; x++){
         if (x == 0) online=api_request("metObsAPI", stations_liste[stations_count].kode);
         if (x == 1) online=api_request("oceanObsAPI", kyst_stations_liste[stations_count].kode);
         if (x == 2) online=api_request("lightObsAPI", kyst_stations_liste[stations_count].kode);
         if (x == 3) online=api_request("climateObsAPI", stations_liste[stations_count].kode);

         if (online == 0){
            mea[x].elapsed_sum10 = mea[x].elapsed_sum10 + mea[x].elapsed;
            mea[x].elapsed_sum100 = mea[x].elapsed_sum100 + mea[x].elapsed;
            mea[x].elapsed_sum1000 = mea[x].elapsed_sum1000 + mea[x].elapsed;
            if (mea[x].elapsed > mea[x].elapsed_high) mea[x].elapsed_high = mea[x].elapsed;
            if (mea[x].elapsed < mea[x].elapsed_low) mea[x].elapsed_low = mea[x].elapsed;
            }
         } /* for */

      // Calculate
      if (online == 0){
         if (g10 == 10) {
            for (x = 0; x <= 3; x++){
               mea[x].elapsed_gns10 = mea[x].elapsed_sum10 / 10;
               mea[x].elapsed_sum10 = 0;

               if (x == 0) snprintf(syslog_txt,79,"metObs avg10= %8.2f", mea[x].elapsed_gns10);
               if (x == 1) snprintf(syslog_txt,79,"oceanObs avg10= %8.2f", mea[x].elapsed_gns10);
               if (x == 2) snprintf(syslog_txt,79,"lightObs avg10= %8.2f", mea[x].elapsed_gns10);
               if (x == 3) snprintf(syslog_txt,79,"climateObs avg10= %8.2f", mea[x].elapsed_gns10);
               if (mea[x].elapsed_gns10 <= atoi(th[x].trs_warning))
                  write_syslog(syslog_txt, 1);
               else if (mea[x].elapsed_gns10 > atoi(th[x].trs_warning) && mea[x].elapsed_gns10 < atoi(th[x].trs_error))
                  write_syslog(syslog_txt, 2);
               else if (mea[x].elapsed_gns10 > atoi(th[x].trs_error))
                  write_syslog(syslog_txt, 3);
               if (x == 0) write_statlog("m10",trans_dato, mea[x].elapsed_gns10, mea[x].elapsed_low, mea[x].elapsed_high);
               if (x == 1) write_statlog("o10",trans_dato, mea[x].elapsed_gns10, mea[x].elapsed_low, mea[x].elapsed_high);
               if (x == 2) write_statlog("l10",trans_dato, mea[x].elapsed_gns10, mea[x].elapsed_low, mea[x].elapsed_high);
               if (x == 3) write_statlog("c10",trans_dato, mea[x].elapsed_gns10, mea[x].elapsed_low, mea[x].elapsed_high);
               } /* for */
            g10 = 0;
         } /* == 10 */

         if (g100 == 100) {
            for (x = 0; x <= 3; x++){
               mea[x].elapsed_gns100 = mea[x].elapsed_sum100 / 100;
               mea[x].elapsed_sum100 = 0;
               if (x == 0) write_statlog("m100",trans_dato, mea[x].elapsed_gns100, mea[x].elapsed_low, mea[x].elapsed_high);
               if (x == 1) write_statlog("o100",trans_dato, mea[x].elapsed_gns100, mea[x].elapsed_low, mea[x].elapsed_high);
               if (x == 2) write_statlog("l100",trans_dato, mea[x].elapsed_gns100, mea[x].elapsed_low, mea[x].elapsed_high);
               if (x == 3) write_statlog("c100",trans_dato, mea[x].elapsed_gns100, mea[x].elapsed_low, mea[x].elapsed_high);
               } /* for */
            g100 = 0;
            } /* == 100 */

         if (g1000 == 1000) {
            for (x = 0; x <= 3; x++){
               mea[x].elapsed_gns1000 = mea[x].elapsed_sum1000 / 1000;
               mea[x].elapsed_sum1000 = 0;
               if (x == 0) write_statlog("m1000",trans_dato, mea[x].elapsed_gns1000, mea[x].elapsed_low, mea[x].elapsed_high);
               if (x == 1) write_statlog("o1000",trans_dato, mea[x].elapsed_gns1000, mea[x].elapsed_low, mea[x].elapsed_high);
               if (x == 2) write_statlog("l1000",trans_dato, mea[x].elapsed_gns1000, mea[x].elapsed_low, mea[x].elapsed_high);
               if (x == 3) write_statlog("c1000",trans_dato, mea[x].elapsed_gns1000, mea[x].elapsed_low, mea[x].elapsed_high);

               // Reset low/high
               mea[x].elapsed_low=1000;
               mea[x].elapsed_high=0;
               }
            g1000 = 0;

            } /* == 1000 */

         g10++;
         g100++;
         g1000++;
      } /* if online=0 */
      current_time=time(NULL);

      // View console & do html output
      view_console();
      html_output();

      stations_count++;
      if (stations_count == 17) stations_count=0;

      sleep(atoi(freq));
   } /* while */

   return 0;
   } /* main */

// Get and interpret data
int api_request(char* api, char* station_id){
   int x, y, z, read_block, http_ok, rc, rc2;
   int api_type = 0; // 0=metObs,1=oceanObs,2=lightObs,3=climateObs
   int http_ret;
   long int length, length_sofar, header_length, ssl_error;
   
   char sendtoserver[512] = {0};
   char server_reply[MAX_BUF] = {0};
   char server_reply2[MAX_BUF] = {0};
   char trans_data[MAX_BUF] = {0};
   char trans_data2[MAX_BUF] = {0};;
   
   char http_ret_code[5] = {0};
   char syslog_str[80] = {0};

   // Create socket
   online = init_com();
   if (TCPIPDEBUG) write_syslog("Efter init_com",5);

   sendtoserver[0]=0;
   if (strcmp(api,"metObsAPI") == 0){ 
      api_type=0;
      strcpy(sendtoserver,"GET /v2/metObs/collections/observation/items?period=latest&parameterId=temp_dry");
      strcat(sendtoserver,"&api-key=");
      strcat(sendtoserver,key_metobs);
      strcat(sendtoserver,"&stationId=");
      strcat(sendtoserver,station_id);
      strcat(sendtoserver," HTTP/1.1\r\nHost:dmigw.govcloud.dk\r\nAccept: application/json\r\n\r\n");
      }
   if (strcmp(api,"oceanObsAPI") == 0){
      api_type=1;
      strcpy(sendtoserver,"GET /v2/oceanObs/collections/observation/items?parameterId=sealev_dvr&period=latest&api-key=");
      strcat(sendtoserver,key_oceanobs);
      strcat(sendtoserver,"&stationId=");
      strcat(sendtoserver,station_id);
      strcat(sendtoserver," HTTP/1.1\r\nHost:dmigw.govcloud.dk\r\nAccept: application/json\r\n\r\n");
      }
   if (strcmp(api,"lightObsAPI") == 0){
      api_type=2;
      strcpy(sendtoserver,"GET /v2/lightningdata/collections/observation/items?");
      strcat(sendtoserver,"&period=latest&api-key=");
      strcat(sendtoserver,key_lightobs);
      strcat(sendtoserver," HTTP/1.1\r\nHost:dmigw.govcloud.dk\r\nAccept: application/json\r\n\r\n");
      }
   if (strcmp(api,"climateObsAPI") == 0){
      api_type=3;
      strcpy(sendtoserver,"GET /v2/climateData/collections/stationValue/items?");
      strcat(sendtoserver,"&parameterId=mean_temp&limit=1&stationId=");
      strcat(sendtoserver,station_id);
      strcat(sendtoserver,"&api-key=");
      strcat(sendtoserver,key_climateobs);
      strcat(sendtoserver," HTTP/1.1\r\nHost:dmigw.govcloud.dk\r\nAccept: application/json\r\n\r\n");
      }

   gettimeofday(&t0, 0); // Measure t0

   // Send data to server
   if (HTTPLOGGING) http_log("[TCPIP Send]%s[EOS]\n", sendtoserver);
   rc = SSL_write(ssl, sendtoserver, strlen(sendtoserver));
   if (rc == 0){
      snprintf(syslog_str, 600, "SSLwrite rc=0");
      http_log("[api_meta]", syslog_str);
      close_com();
      return 2;
      }

   // Read from server
   server_reply2[0] = 0;
   rc2 = 2048; //First packetsize
   read_block = 0;
   do {
      server_reply[0] = 0;
      if (read_block == 0){ // First packet - get length from header
          rc = SSL_read(ssl, server_reply, rc2);
          if (rc >= 0)
             server_reply[rc] = 0;
          else {
             server_reply[0] = 0;
             break;
             }
          strcat(server_reply2, server_reply);
          length = get_length(server_reply);

          if (HTTPLOGGING) {
             snprintf(syslog_str, 600, "[Length]%i[bytes]", length);
             http_log("[api_meta]", syslog_str);
             }

          if (length == 0 || length >= MAX_BUF-2){ // Message > MAX_BUF
             server_reply[0] = 0;
             break;
             }

          // Length of header
          header_length = 0;
          y = 0;
          while (y < strlen(server_reply)){
             if (server_reply[y] == 0x0d && server_reply[y + 1] == 0x0a &&
                server_reply[y + 2] == 0x0d && server_reply[y + 3] == 0x0a){
                header_length = y;
                break;
                }
             y++;
             }
   
          read_block = 1; // Next chunk
         // Does end of packet contain a 0? (=end of transmission)
         x = strlen(server_reply);
         if (server_reply[x - 5] == '0')
            break;
          }
      else {
         server_reply[0] = 0;
         rc = SSL_read(ssl, server_reply, 2048);
         if (rc > 0)
            server_reply[rc] = 0;
         else{
            server_reply[0] = 0;
            break;
            }

         length = get_length(server_reply);

         if ((strlen(server_reply2) + strlen(server_reply)) > MAX_BUF-2){
            snprintf(syslog_str,79,"Object to big - skipped"); // Message > MAX_BUF
            write_syslog(syslog_str, 2);
            close_com();
            return 3;
            }

         // Where does data start? 
         x = 0;
         while (x < strlen(server_reply)){
           if (server_reply[x] == 0x0d && server_reply[x + 1] == 0x0a){ 
              y = x;
              break;
              }
            x++;
            } /* while */

         // Copy from offset
         x = x + 2;
         y = 0;
         z = strlen(server_reply2) - 2;
         while (server_reply[y + x] != 0){
            server_reply2[z] = server_reply[y + x]; 
            server_reply2[z + 1] = 0;
            y++;
            z++;
            }

         // Does end of packet contain a 0? (=end of transmission)
         x = strlen(server_reply);
         if (server_reply[x - 5] == '0')
            break;

         if (length == 0){
             break; // All is read
             }

         } /* else */
      } while (rc > 0);
      if (rc < 0){
         switch(ssl_error = SSL_get_error(ssl, rc)){
            case SSL_ERROR_NONE:
               if (TCPIPDEBUG) write_syslog("SSL_ERROR_NONE", 1);
               break;
            case SSL_ERROR_WANT_READ:
               if (TCPIPDEBUG) write_syslog("SSL_WANT_READ", 2);
               break;
            case SSL_ERROR_ZERO_RETURN:
               if (TCPIPDEBUG) write_syslog("SSL_ZERO_RETURN", 2);
               break;
            case SSL_ERROR_SYSCALL:
               if (TCPIPDEBUG) write_syslog("SSL_ERROR_SYSCALL", 3);
               break;
            case SSL_ERROR_WANT_WRITE:
               if (TCPIPDEBUG) write_syslog("SSL_ERROR_WANT_WRITE", 2);
               break;
            default:
               if (TCPIPDEBUG) write_syslog("UNKNOWN SSL_get_error", 3);
         }
      }
   gettimeofday(&t1, 0); // Measure t1

   strcpy(server_reply, server_reply2);
   close_com();
   if (HTTPLOGGING) http_log("[HTML Received]%s[EOS]", server_reply);

   mea[api_type].elapsed = timedifference_msec(t0, t1);

   if (strlen(server_reply) <= 50){ /* No data from socket */
      mea[api_type].elapsed = 0;
      snprintf(syslog_str,79,"Error: Returncode: Only %i bytes recieved from API %i", strlen(server_reply), api_type);
      write_syslog(syslog_str,2);
      return -1;
      }
   
   // Decode HTTP-returncode
   http_ok = 99;
   http_ret = 999;
   if (strlen(server_reply) > 12){
      http_ret_code[0] = server_reply[9];
      http_ret_code[1] = server_reply[10];
      http_ret_code[2] = server_reply[11];
      http_ret_code[3] = 0;
      http_ret =  atoi(http_ret_code);
      } 
   else {
      snprintf(syslog_str,79,"Error: Less than %i bytes recieved from API %i", strlen(server_reply), api_type);
      write_syslog(syslog_str,2);
      return -2;
      }

   snprintf(syslog_str,79,"HTTP %i recieved from API %i", http_ret, api_type);

   switch(http_ret) {
     case 200:		// Ok
        mea[api_type].requests++;
	mea[api_type].last_returncode = 200;
        break;
     case 204:		// No content
        http_resp[api_type].http_204++;
        strcpy(observation[api_type].data,"No data (http 204)");
	mea[api_type].last_returncode = 204;
        write_syslog(syslog_str,2);
        break;
     case 400:		// Bad request
        http_resp[api_type].http_other++;
	mea[api_type].last_returncode = 400;
        strcpy(observation[api_type].data,"Bad request (http 400)");
        write_syslog(syslog_str,2);
        break;
     case 401:		// Unauthorized
        http_resp[api_type].http_other++;
	mea[api_type].last_returncode = 401;
        strcpy(observation[api_type].data,"Unauthorized (http 401)");
        write_syslog(syslog_str,2);
        break;
     case 404:		// Not found
        http_resp[api_type].http_other++;
	mea[api_type].last_returncode = 404;
        strcpy(observation[api_type].data,"Not found (http 404)");
        write_syslog(syslog_str,2);
        break;
     case 408:		// Request timeout
        http_resp[api_type].http_other++;
	mea[api_type].last_returncode = 408;
        strcpy(observation[api_type].data,"Request timeout(http 408)");
        write_syslog(syslog_str,2);
        break;
     case 999:		// Unexpected data from server
        http_resp[api_type].http_other++;
	mea[api_type].last_returncode = 999;
        strcpy(observation[api_type].data,"Unexpected data from server");
        write_syslog(syslog_str,2);
        break;
     default:		// All other http-codes
        http_resp[api_type].http_other++;
	mea[api_type].last_returncode = 999;
        strcpy(observation[api_type].data,"Unexpected http-returncode ");
        write_syslog(syslog_str,2);
     } /* switch */

   // Decode API-transactioncode
   strcpy(trans_data2,"No Transactioncode");
   if (http_ret == 200 ) { 		// returkode = 200 
      // Decode transactioncode
     if (strstr(server_reply, "x-gravitee-transaction-id:") != NULL)  strcpy(trans_data, strstr(server_reply, "x-gravitee-transaction-id:"));
      if (strlen(trans_data) > 0){
         x = y = 0;
         while (trans_data[x] != ':' && x < strlen(trans_data))
            x++;
         x=x+2; 
         while(trans_data[x] != 10 && x < strlen(trans_data)){   
            trans_data2[y] = trans_data[x];
            y++;
            x++;
            }
            trans_data2[y - 1]=0;
         } /* if */
      }  

   // Decode API-transactiondate
   if (http_ret == 200 || http_ret == 204 ) {   // Assume only ret.code 200 & 2034 gives timestamp
   strcpy(trans_dato,"01 Jan 1970 00:00:00 GMT");
   if (strstr(server_reply,"date:") != NULL) strcpy(trans_data,strstr(server_reply,"date:"));
   if (strlen(trans_data) > 0){
      x=y=0;
      while (trans_data[x] != ':' && x < 1999) x++;
      x=x+7; // Remove dayname & ','
      while(trans_data[x] != 10 && x < 1999){
         trans_dato[y]=trans_data[x];
         y++;
         x++;
         }
         trans_dato[y-1]=0;
         } /* if */
      } /* if */
      
   write_translog(trans_dato, api_type, http_ret, trans_data2, timedifference_msec(t0, t1));
   decode_data(api_type, server_reply);
   return 0;
   
   } /* api_request */

int decode_data(int api, char* server_reply){
   int x, y, i, j, k;
   char data_temp[80] = {0};
   char data_temp2[80] = {0};
   char data[2000] = {0};
   char json_str[5000] = {0};
   json_bool json_rc = FALSE, json_rc2 = FALSE;

   // JSON variables
   struct json_object *root, *temp, *temp2, *features, *properties, *value, *amp, *observed;

   strcpy(data_temp, "No value");

   // Isolate json-string
   if (strstr(server_reply, "{") != NULL && (strlen(server_reply) < 5000)) strcpy(json_str,strstr(server_reply, "{"));
   else {
      json_str[0] = 0; //ensure that we dont parse if JSON-object not present
      return 2; // no data
      }

   // Does end of packet contain a 0? (=end of transmission)
   x = strlen(server_reply);
   if (server_reply[x - 5] == '0')
      server_reply[x - 5] = 0;

   root = json_tokener_parse(json_str);

   // Decode data-string - metObs/oceanObs/climateObs
   if (api == 0 || api == 1 || api == 3){
      json_rc = json_object_object_get_ex(root, "features", &features);

      if (json_rc == TRUE){
         temp = json_object_array_get_idx(features, 0);
         json_rc2 = json_object_object_get_ex(temp, "properties", &properties);

         if (json_rc2 == TRUE) {
            json_rc = json_object_object_get_ex(properties, "value", &value);
            if (json_rc == TRUE)
                  sprintf(observation[api].data, "%2.1f", json_object_get_double(value));
               else 
                  strcpy(observation[api].data, "No data");
            }
         }
      } /* if api=0,1,3 */

   /* Decode lightObs */
   if (api == 2){
      json_rc = json_object_object_get_ex(root, "features", &features);

      if (json_rc == TRUE){
         temp = json_object_array_get_idx(features, 0);
         json_rc2 = json_object_object_get_ex(temp, "properties", &properties);

         if (json_rc2 == TRUE) {
            json_rc = json_object_object_get_ex(properties, "amp", &amp);
            if (json_rc == TRUE){
               sprintf(observation[api].data, "%2.1f", json_object_get_double(amp));
               strcat(observation[api].data, " Ampere, t = ");
               }
            else {
               strcpy(observation[api].data, "No amp data");
               }
            json_rc = json_object_object_get_ex(properties, "observed", &observed);
            if (json_rc == TRUE){
               strcat(observation[api].data, json_object_get_string(observed));
               }
            else {
               strcpy(observation[api].data, "No time data");
               }
            }
         }
      } /* if api==2 */
   json_rc = json_object_put(root);
   } /* decode_data */

// View console
void view_console(){
   int x;

   getrusage(RUSAGE_SELF,&r_usage);
   strcpy(screen[0].line,"");
   snprintf(screen[1].line, 130, "DMI API response monitor [%s]   : Latest com.rc:[%i] Mem:[%ld]", VERSION, online, r_usage.ru_maxrss);
   snprintf(screen[2].line, 130, "System start time                 : %s", start_c_time_string);
   snprintf(screen[3].line, 130, "Latest measurement                : %s", ctime(&current_time));
   strcpy(screen[4].line, "MetObsAPI");
   snprintf(screen[5].line, 130, "Latest datapoint                  : %6s C (temp 2m) @ %s", observation[0].data, stations_liste[stations_count].navn);
   snprintf(screen[6].line, 130, "Resp.time latest trans.    (msec) : %8.2f", mea[0].elapsed);
   snprintf(screen[7].line, 130, "Resp.time low/high         (msec) : %8.2f / %8.2f", mea[0].elapsed_low, mea[0].elapsed_high);
   snprintf(screen[8].line, 130, "Resp.time avg. 10/100/1000 (msec) : %8.2f / %8.2f / %8.2f", mea[0].elapsed_gns10, mea[0].elapsed_gns100, mea[0].elapsed_gns1000);
   snprintf(screen[9].line, 130, "# req./ret=204/ret=other          : %8i / %8i / %8i", mea[0].requests, http_resp[0].http_204, http_resp[0].http_other);
   strcpy(screen[10].line," ");
   strcpy(screen[11].line,"oceanObsAPI");
   snprintf(screen[12].line, 130, "Latest datapoint                  : %6s cm (sealevel DVR) @ %s", observation[1].data, kyst_stations_liste[stations_count].navn);
   snprintf(screen[13].line, 130, "Resp.time latest.trans     (msec) : %8.2f", mea[1].elapsed);
   snprintf(screen[14].line, 130, "Rest.time low/high         (msec) : %8.2f / %8.2f", mea[1].elapsed_low,mea[1].elapsed_high);
   snprintf(screen[15].line, 130, "Resp.time avg. 10/100/1000 (msec) : %8.2f / %8.2f / %8.2f", mea[1].elapsed_gns10, mea[1].elapsed_gns100, mea[1].elapsed_gns1000);
   snprintf(screen[16].line, 130, "# req./ret=204/ret=other          : %8i / %8i / %8i", mea[1].requests, http_resp[1].http_204, http_resp[1].http_other);
   strcpy(screen[17].line," ");
   strcpy(screen[18].line, "lightningObsApi");
   snprintf(screen[19].line, 130, "Latest datapoint                  : %s", observation[2].data);
   snprintf(screen[20].line, 130, "Resp.time latest trans     (msec) : %8.2f", mea[2].elapsed);
   snprintf(screen[21].line, 130, "Resp.time low/high         (msec) : %8.2f / %8.2f", mea[2].elapsed_low, mea[2].elapsed_high);
   snprintf(screen[22].line, 130, "Resp.time avg. 10/100/1000 (msec) : %8.2f / %8.2f / %8.2f", mea[2].elapsed_gns10, mea[2].elapsed_gns100, mea[2].elapsed_gns1000);
   snprintf(screen[23].line, 130, "# req./ret=204/ret=other          : %8i / %8i / %8i", mea[2].requests, http_resp[2].http_204, http_resp[2].http_other);
   strcpy(screen[24].line," ");
   strcpy(screen[25].line, "climateObsApi");
   snprintf(screen[26].line, 130, "Latest datapoint                  : %6s C (mean temp) @ %s", observation[3].data, stations_liste[stations_count].navn);
   snprintf(screen[27].line, 130, "Resp.time latest trans.    (msec) : %8.2f", mea[3].elapsed);
   snprintf(screen[28].line, 130, "Resp.time low/high         (msec) : %8.2f / %8.2f", mea[3].elapsed_low, mea[3].elapsed_high);
   snprintf(screen[29].line, 130, "Resp.time avg. 10/100/1000 (msec) : %8.2f / %8.2f / %8.2f", mea[3].elapsed_gns10, mea[3].elapsed_gns100, mea[3].elapsed_gns1000);
   snprintf(screen[30].line, 130, "# req./ret=204/ret=other          : %8i / %8i / %8i", mea[3].requests, http_resp[3].http_204, http_resp[3].http_other);
   strcpy(screen[31].line," ");

   // View
   if (atoi(silent) == 1){
      printf("\e[1;1H\e[2J"); // Clear screen
      for (x=0;x<=31;x++)
        printf("%s\n",screen[x].line);
      } /* if */
   } /* view_console */

// Write html-page with console-output
void html_output(){
   int x;

   compute_colors();

   http_out = fopen("index.html", "w");
   fprintf(http_out,"<!DOCTYPE html><html>\n<head><style> body { font-family: 'Courier New', monospace; } </style></head> <meta charset=\"UTF-8\"> <body><pre>\n"); 
// Print header
   fprintf(http_out, "<b><h1>Statens It - Service Operation Center </b></h1>", screen[x].line);
   for (x = 0; x <= 3; x++)
      fprintf(http_out, "%s<br>", screen[x].line);

   fprintf(http_out, "<h2><b>%smetObsAPI%s</b></h2>", mea[0].elapsed_gns10_html_color, HTML_END);
   fprintf(http_out, "Latest datapoint                  : %6s C (temp 2m) @ %s<br>", observation[0].data, stations_liste[stations_count].navn);
   fprintf(http_out, "Resp.time latest trans.    (msec) : [%s%8.2f%s]<br>", mea[0].elapsed_html_color, mea[0].elapsed, HTML_END);
   fprintf(http_out, "Resp.time low/high         (msec) : [%s%8.2f%s] / [%s%8.2f%s]<br>", mea[0].elapsed_low_html_color, mea[0].elapsed_low, HTML_END, mea[0].elapsed_high_html_color, mea[0].elapsed_high, HTML_END);
   fprintf(http_out, "Resp.time avg. 10/100/1000 (msec) : [%s%8.2f%s] / [%s%8.2f%s] / [%s%8.2f%s]<br>", mea[0].elapsed_gns10_html_color, mea[0].elapsed_gns10, HTML_END, mea[0].elapsed_gns100_html_color, mea[0].elapsed_gns100, HTML_END, mea[0].elapsed_gns1000_html_color, mea[0].elapsed_gns1000, HTML_END);
   fprintf(http_out, "%s# req./ret=204/ret=other          : %8i / %8i / %8i%s", mea[0].last_returncode_html_color, mea[0].requests, http_resp[0].http_204, http_resp[0].http_other, HTML_END);

   fprintf(http_out, "<br><h2><b>%sOceanObsAPI%s</b></h2>", mea[1].elapsed_gns10_html_color, HTML_END);
   fprintf(http_out, "Latest datapoint                  : %6s cm (sealevel DVR) @ %s<br>", observation[1].data, kyst_stations_liste[stations_count].navn);
   fprintf(http_out, "Resp.time latest trans.    (msec) : [%s%8.2f%s]<br>", mea[1].elapsed_html_color, mea[1].elapsed, HTML_END);
   fprintf(http_out, "Resp.time low/high         (msec) : [%s%8.2f%s] / [%s%8.2f%s]<br>", mea[1].elapsed_low_html_color, mea[1].elapsed_low, HTML_END, mea[1].elapsed_high_html_color, mea[1].elapsed_high, HTML_END);
   fprintf(http_out, "Resp.time avg. 10/100/1000 (msec) : [%s%8.2f%s] / [%s%8.2f%s] / [%s%8.2f%s]<br>", mea[1].elapsed_gns10_html_color, mea[1].elapsed_gns10, HTML_END, mea[1].elapsed_gns100_html_color, mea[1].elapsed_gns100, HTML_END, mea[1].elapsed_gns1000_html_color, mea[1].elapsed_gns1000, HTML_END);
   fprintf(http_out, "%s# req./ret=204/ret=other          : %8i / %8i / %8i%s", mea[1].last_returncode_html_color,mea[1].requests, http_resp[1].http_204, http_resp[1].http_other, HTML_END);

   fprintf(http_out, "<br><h2><b>%sLightningObsAPI%s</b></h2>", mea[2].elapsed_gns10_html_color, HTML_END);
   fprintf(http_out, "Latest datapoint                  : %s<br>", observation[2].data);
   fprintf(http_out, "Resp.time latest trans.    (msec) : [%s%8.2f%s]<br>", mea[2].elapsed_html_color, mea[2].elapsed, HTML_END);
   fprintf(http_out, "Resp.time low/high         (msec) : [%s%8.2f%s] / [%s%8.2f%s]<br>", mea[2].elapsed_low_html_color, mea[2].elapsed_low, HTML_END, mea[2].elapsed_high_html_color, mea[2].elapsed_high, HTML_END);
   fprintf(http_out, "Resp.time avg. 10/100/1000 (msec) : [%s%8.2f%s] / [%s%8.2f%s] / [%s%8.2f%s]<br>", mea[2].elapsed_gns10_html_color, mea[2].elapsed_gns10, HTML_END, mea[2].elapsed_gns100_html_color, mea[2].elapsed_gns100, HTML_END, mea[2].elapsed_gns1000_html_color, mea[2].elapsed_gns1000, HTML_END);
   fprintf(http_out, "%s# req./ret=204/ret=other          : %8i / %8i / %8i%s", mea[2].last_returncode_html_color, mea[2].requests, http_resp[2].http_204, http_resp[2].http_other, HTML_END);

   fprintf(http_out, "<br><h2><b>%sClimateObsAPI%s</b></h2>", mea[3].elapsed_gns10_html_color, HTML_END);
   fprintf(http_out, "Latest datapoint                  : %6s C (mean temp) @ %s<br>", observation[3].data, stations_liste[stations_count].navn);
   fprintf(http_out, "Resp.time latest trans.    (msec) : [%s%8.2f%s]<br>", mea[3].elapsed_html_color, mea[3].elapsed, HTML_END);
   fprintf(http_out, "Resp.time low/high         (msec) : [%s%8.2f%s] / [%s%8.2f%s]<br>", mea[3].elapsed_low_html_color, mea[3].elapsed_low, HTML_END, mea[3].elapsed_high_html_color, mea[3].elapsed_high, HTML_END);
   fprintf(http_out, "Resp.time avg. 10/100/1000 (msec) : [%s%8.2f%s] / [%s%8.2f%s] / [%s%8.2f%s]<br>", mea[3].elapsed_gns10_html_color, mea[3].elapsed_gns10, HTML_END, mea[3].elapsed_gns100_html_color, mea[3].elapsed_gns100, HTML_END, mea[3].elapsed_gns1000_html_color, mea[3].elapsed_gns1000, HTML_END);
   fprintf(http_out, "%s# req./ret=204/ret=other          : %8i / %8i / %8i%s", mea[3].last_returncode_html_color, mea[3].requests, http_resp[3].http_204, http_resp[3].http_other, HTML_END);

   fclose(http_out);
   } /* html_output */

// Write translog-event
void write_translog(char* trans_date, int api_id, int http_code, char* trans_id, double trans_tid){
   char name[40];

   // One file per day
   time(&file_current_time);
   today = localtime(&file_current_time);
   snprintf(name, 40, "%0d-%0d-%0d_dmiapi.trans", today->tm_year+1900, today->tm_mon+1, today->tm_mday);

   translog_out = fopen(name, "a+");
   fprintf(translog_out,"%10s,%1i,%3i,%s,%8.2f\n",trans_date ,api_id, http_code,trans_id, trans_tid);

   fclose(translog_out);
   } /* write_translog */

// Write statlog-event
void write_statlog(char* trans_type, char* trans_date, double trans_tid, double low, double high){
   char name[40];

   // One file per day
   time(&file_current_time);
   today = localtime(&file_current_time);
   snprintf(name, 40, "%0d-%0d-%0d_dmiapi.stat", today->tm_year+1900, today->tm_mon+1, today->tm_mday);

   statlog_out = fopen(name, "a+");
   fprintf(statlog_out,"%10s,%5s,%8.2f,%8.2f,%8.2f\n",trans_date, trans_type, trans_tid, low, high);
   fclose(statlog_out);
   } /* write_statlog */

// Write syslog & local syslog-file
void write_syslog(const char* msg, int pri){
   char name[40], log_time[40];

   // Write in application-log
   // One file per day
   time(&file_current_time);
   today = localtime(&file_current_time);
   snprintf(name, 40, "%0d-%0d-%0d_dmiapi.log", today->tm_year+1900, today->tm_mon+1, today->tm_mday);
   snprintf(log_time, 40, "%02d.%02d.%04d %02d:%02d:%02d", today->tm_mday, today->tm_mon+1,today->tm_year+1900, today->tm_hour, today->tm_min, today->tm_sec);

   syslog_out = fopen(name, "a+");

   if (pri == 0){
      fprintf(syslog_out,"%s DMIAPI[%i]: (INFO) %s\n",log_time, pri, msg);
      }   
   if (pri == 1){
      fprintf(syslog_out,"%s DMIAPI[%i]: (NOTICE) %s\n",log_time, pri, msg);
      }
   if (pri == 2){ 
      fprintf(syslog_out,"%s DMIAPI[%i]: (WARNING) %s\n", log_time, pri, msg);
      }
   if (pri == 3){ 
      fprintf(syslog_out,"%s DMIAPI[%i]: (ERROR) %s\n", log_time, pri, msg);
      }
   fclose(syslog_out);

   // Write in Linux syslog
   openlog("DMIAPI", LOG_PID | LOG_NDELAY | LOG_CONS, LOG_MAIL);
   setlogmask(LOG_UPTO(LOG_DEBUG));

   if (pri == 0) syslog(LOG_INFO, "DMIAPI: (INFO) %s", msg);
   if (pri == 1) syslog(LOG_NOTICE, "DMIAPI: (NOTICE) %s", msg);
   if (pri == 2) syslog(LOG_WARNING, "DMIAPI: (WARNING) %s", msg);
   if (pri == 3) syslog(LOG_ERR, "DMIAPI: (ERROR) %s", msg);
   closelog();
   } /* write_syslog */

int goodbye(int status_code){
   fclose(http_debug_file);
   fclose(config_file);
   write_syslog("Program ended", status_code);
   exit(status_code);
   } /* goodbye */

// Read configuration
void read_config(char* config_filename){
char parameter[200], value[200];
int x,y;

   config_file=fopen(config_filename, "r");
   if (config_file == NULL){
      printf("DMIAPI: Konfigurationsfil findes ikke - afslutter.\n");
      write_syslog("Error opening config-file - terminating", 3);
      goodbye(3);
      }

   while (fscanf(config_file,"%s %s", parameter, value) == 2){
      if (strcmp(parameter, "[USERID]") == 0) strcpy(userid, value); else
      if (strcmp(parameter, "[IPHOST]") == 0) strcpy(iphost, value); else
      if (strcmp(parameter, "[FREQ]") == 0) strcpy(freq, value); else
      if (strcmp(parameter, "[WWW-PATH]") == 0) strcpy(wwwpath, value); else
      if (strcmp(parameter, "[METOBSKEY]") == 0) strcpy(key_metobs, value); else
      if (strcmp(parameter, "[OCEANOBSKEY]") == 0) strcpy(key_oceanobs, value); else
      if (strcmp(parameter, "[LIGHTOBSKEY]") == 0) strcpy(key_lightobs, value); else
      if (strcmp(parameter, "[CLIMATEOBSKEY]") == 0) strcpy(key_climateobs, value); else
      if (strcmp(parameter, "[METOBS_THRESHOLD_WARNING]") == 0) strcpy(th[0].trs_warning, value); else
      if (strcmp(parameter, "[METOBS_THRESHOLD_ERROR]") == 0) strcpy(th[0].trs_error, value); else
      if (strcmp(parameter, "[OCEANOBS_THRESHOLD_WARNING]") == 0) strcpy(th[1].trs_warning, value); else
      if (strcmp(parameter, "[OCEANOBS_THRESHOLD_ERROR]") == 0) strcpy(th[1].trs_error, value); else
      if (strcmp(parameter, "[LIGHTOBS_THRESHOLD_WARNING]") == 0) strcpy(th[2].trs_warning, value); else
      if (strcmp(parameter, "[LIGHTOBS_THRESHOLD_ERROR]") == 0) strcpy(th[2].trs_error, value); else
      if (strcmp(parameter, "[CLIMATEOBS_THRESHOLD_WARNING]") == 0) strcpy(th[3].trs_warning, value); else
      if (strcmp(parameter, "[CLIMATEOBS_THRESHOLD_ERROR]") == 0) strcpy(th[3].trs_error, value); else
      if (strcmp(parameter, "[SILENT]") == 0) strcpy(silent, value);
      else {
         write_syslog("Unknown parameter in configurationfile - terminating", 3);
         printf("DMIAPI: Unknown parameter id in configurationfile: [%s]  - terminating", config_filename);
         goodbye(3); 
      }
   } /* while */
   fclose(config_file);

   // Check: Parameters contain some value
   if (strlen(wwwpath) == 0 || strlen(freq) == 0 || strlen(iphost) == 0 || strlen(key_metobs) == 0 || strlen(userid) == 0 ||
      strlen(key_oceanobs) == 0 || strlen(key_lightobs) == 0 || strlen(key_climateobs) == 0 || 
      strlen(th[0].trs_warning) == 0 || strlen(th[0].trs_error) == 0 ||
      strlen(th[1].trs_warning) == 0 || strlen(th[1].trs_error) == 0 || 
      strlen(th[2].trs_warning) == 0 || strlen(th[2].trs_error) == 0 || 
      strlen(th[3].trs_warning) == 0 || strlen(th[3].trs_error) == 0 ) {
      printf("DMIAPI: Error in konfigurationfile: Missing parameters - terminating\n");
      write_syslog("Error in configurationfile - terminating", 3);
      goodbye(3);
      }

   // Check: 1 <= [FREQ] < 32768
   if (atoi(freq) <= 1 || atoi(freq) > 32769){
      printf("DMIAPI: [FREQ] must be between 1 and 32768 - terminating\n");
      write_syslog("[FREQ] must be betwwen 1 and 32768 - terminating", 3);
      goodbye(3);
      } 

   for (x = 0; x < 4; x++){
      // Check: 10 < [THRESHOLD_WARNING] < 10000
      if (atoi(th[x].trs_warning) < 10 || atoi(th[x].trs_warning) > 10000){
         printf("DMIAPI: [THRESHOLD_WARNING] must be between 10 and 10000 - terminating\n");
         write_syslog("[THRESHOLD_WARNING] must be between 10 and 10000 - terminating", 3);
         goodbye(3);
         } 

      // Check: 10 < [THRESHOLD_ERROR] < 10000
      if (atoi(th[x].trs_error) < 10 || atoi(th[x].trs_error) > 10000){
         printf("DMIAPI: [THRESHOLD_ERROR] must be between 10 and 10000 - terminating\n");
         write_syslog("[THRESHOLD_ERROR] must be between 10 and 10000 - terminating", 3);
         goodbye(3);
         } 
      }

   // Check: [SILENT] must be 0 or 1
   if (strcmp(silent,"0") != 0 && strcmp(silent,"1") != 0){
      printf("DMIAPI: [SILENT] must be 0 or 1 - terminating\n");
      write_syslog("[SILENT] must be 0 or 1 - terminating", 3);
      goodbye(3);
      }
   } /* read_config */

// Calculate timediff. in ms
float timedifference_msec(struct timeval t0, struct timeval t1) {
   return (t1.tv_sec - t0.tv_sec) * 1000.0f + (t1.tv_usec - t0.tv_usec) / 1000.0f;
   } /* timedifference_msec */

// Create socket
int create_socket(char url_str[], BIO *out) {
   int sockfd;
   char hostname[256] = "";
   char portnum[6] = "443";
   char proto[6] = "";
   char *tmp_ptr = NULL;
   int port;
   struct hostent *host;
   struct sockaddr_in dest_addr;
   struct timeval tv;
   char syslog_str[80] = {0};

   if (url_str[strlen(url_str)] == '/')
      url_str[strlen(url_str)] = '\0';

   strncpy(proto, url_str, (strchr(url_str, ':') - url_str));
   strncpy(hostname, strstr(url_str, "://") + 3, sizeof(hostname));
   if (strchr(hostname, ':')) {
      tmp_ptr = strchr(hostname, ':');
      strncpy(portnum, tmp_ptr+1,  sizeof(portnum));
      *tmp_ptr = '\0';
      }
   port = atoi(portnum);

   if ((host = gethostbyname(hostname)) == NULL ) {
      strcpy(syslog_str, "Cant resolve hostname: ");
      strcat(syslog_str, hostname);
      write_syslog(syslog_str, 2);
      return 0;
      }

   // create the basic TCP socket
   sockfd = socket(AF_INET, SOCK_STREAM, 0);
   if (sockfd == -1){
      strcpy(syslog_str, "Cant create socket: ");
      strcat(syslog_str, hostname);
      write_syslog(syslog_str, 2);
      close(sockfd);
      return 0;
      }

   dest_addr.sin_family = AF_INET;
   dest_addr.sin_port=htons(port);
   dest_addr.sin_addr.s_addr = *(long*)(host->h_addr);

   // set timeout
   tv.tv_sec = 5;
   tv.tv_usec = 0;
   setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));

   // Zeroing the rest of the struct
   memset(&(dest_addr.sin_zero), '\0', 8);

   tmp_ptr = inet_ntoa(dest_addr.sin_addr);

   // Try to make the host connect here
   if (connect(sockfd, (struct sockaddr *) &dest_addr, sizeof(struct sockaddr)) == -1 ) {
      strcpy(syslog_str, "Cant connect to hostname: ");
      strcat(syslog_str, hostname);
      write_syslog(syslog_str, 2);
      close(sockfd);
      }

   return sockfd;
   } /* create socket */

int init_com(){
   int rc;
   char syslog_str[80] = {0};

   // Create input/output BIO's
   certbio = BIO_new(BIO_s_file());
   outbio  = BIO_new_fp(stdout, BIO_NOCLOSE);

   // Initialize SSL Lib
   if (SSL_library_init() < 0){
      write_syslog("Could not initialize the OpenSSL library.", 3);
      return 0;
      }
   method = SSLv23_client_method();

   // Create SSL context
   if ((ctx = SSL_CTX_new(method)) == NULL){
      strcpy(syslog_str, "Unable to create a new SSL context structure.");
      write_syslog(syslog_str, 2);
      }
   SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2);
   ssl = SSL_new(ctx);

   // create TCPIP connection
   server = create_socket(iphost, outbio);
   if (server == 0){
      // Clean up
      SSL_CTX_free(ctx);
      SSL_free(ssl);
      BIO_free_all(certbio);
      BIO_free_all(outbio);

      strcpy(syslog_str, "Unable to establish tcp/ip connection.");
      write_syslog(syslog_str, 2);
      return 0;
      }

   if ((server != 0) && TCPIPDEBUG)
      BIO_printf(outbio, "Successfully made the TCP connection to: %s.\n", iphost);

   // Attach SSL to connection
   rc = SSL_set_fd(ssl, server);
   if (rc == 1)
      rc = SSL_connect(ssl);
   if (TCPIPDEBUG) log_ssl();
   if (rc != 1){
      SSL_CTX_free(ctx);
      SSL_free(ssl);
      close(server);
      BIO_free_all(certbio);
      BIO_free_all(outbio);
      strcpy(syslog_str, "Could not build a SSL session.");
      write_syslog(syslog_str, 2);
      }
    else
       if (TCPIPDEBUG) BIO_printf(outbio, "Successfully enabled SSL/TLS session to: %s.\n", iphost);

   // Get certificate
   if (rc == 1) cert = SSL_get_peer_certificate(ssl);
   if (cert == NULL){
      // Clean up
      SSL_CTX_free(ctx);
      SSL_free(ssl);
      close(server);
      BIO_free_all(certbio);
      BIO_free_all(outbio);

      strcpy(syslog_str, "Could not get certificate for.");
      write_syslog(syslog_str, 2);
      }
   else
      if (TCPIPDEBUG) BIO_printf(outbio, "Retrieved the server's certificate from: %s.\n", iphost);

   // Display cert
   if (TCPIPDEBUG){
      BIO_printf(outbio, "Displaying the certificate subject data:\n");
      X509_NAME_print_ex(outbio, certname, 0, 0);
      BIO_printf(outbio, "\n");
      }
   if (TCPIPDEBUG) write_syslog("End init_com",5);
   return rc;
   } /* init_com */

void close_com(){
   SSL_free(ssl);
   close(server);
   SSL_CTX_free(ctx);
   BIO_free_all(certbio);
   BIO_free_all(outbio);
   X509_free(cert);
   if (TCPIPDEBUG) BIO_printf(outbio, "Finished SSL/TLS connection with server: %s.\n", iphost);
   } /* close_com */

// Read out SSL errors
int log_ssl(void) {
   char buf[256];
   u_long err;

   while ((err = ERR_get_error()) != 0) {
      ERR_error_string_n(err, buf, sizeof(buf));
      }
   return 0;
   }

void http_log(char* msg1, char* msg2){
   http_debug_file = fopen("dmiapi_http.log", "a+");
   fprintf(http_debug_file, "%s %s\n", msg1, msg2);
   fclose(http_debug_file);
   } /* http_log */

// Get length from header
long unsigned int get_length(char* buffer){
   char content_str[128] = {0};
   char str_size[20] = {0};
   char *ptr;
   int x, y,  header_length;
   long int len;

   if (strstr(buffer, "Content-Length:") != 0){  // Non chunked data
      strncpy(content_str, strstr(buffer, "Content-Length:"),100);
      x = 0;
      do {
         str_size[x] = content_str[x + 16];
         x++;
         } while ((content_str[x + 16] >= '0') && (content_str[x + 16] <= '9') && (x < 20) && content_str[x + 16] != 0x0d && content_str[x + 17] != 0x0a);
      if (atoi(str_size) > 0) return atoi(str_size); else return 0;
      } /* if */

   else {
      if (strstr(buffer, "transfer-encoding: chunked") != 0){
         // Read until end of header
         x = 0;
         header_length = 0;
         while (x < strlen(buffer)){
            if (buffer[x] == 0x0d && buffer[x + 1] == 0x0a &&
               buffer[x + 2] == 0x0d && buffer[x + 3] == 0x0a){
               header_length = x;
               break;
               }
            x++;
           } /* while */
        if (header_length < strlen(buffer)) header_length = header_length + 4; //4: 0x0d 0x0a 0x0d 0x0a

        x = header_length; //read msg-length
        y = 0;
        while (buffer[x] != '{'){
           str_size[y] = buffer[x];
           y++;
           x++; 
           }
        str_size[y] = 0;
        len = strtol(str_size,&ptr,16);
        return len;
        } /*  if */
   } /* else */

   // no header data = 2-n.th. chunk
   x = 0;
   header_length = 0;
   while (x < strlen(buffer)){
      str_size[x] = buffer[x];
      if (buffer[x] == 0x0d && buffer[x + 1] == 0x0a){
         str_size[x + 1] = 0;
         break;
         }
      x++;
      } /* while */
   len = strtol(str_size,&ptr,16);
   return len;
   } /* get_length */

// Colorcodes for HTML-output
void compute_colors(){
   int x;

   for (x = 0; x <= NUM_OF_APIS; x++){
      // elapsed meta
      if (mea[x].elapsed < atoi(th[x].trs_warning)) {
         strcpy(mea[x].elapsed_html_color, HTML_GREEN);
         }
      if ((mea[x].elapsed > atoi(th[x].trs_warning)) && (mea[x].elapsed < atoi(th[x].trs_error))){
         strcpy(mea[x].elapsed_html_color, HTML_YELLOW);
         }
      if (mea[x].elapsed > atoi(th[x].trs_error)) {
         strcpy(mea[x].elapsed_html_color, HTML_RED);
         }

      // elapsed low
      if (mea[x].elapsed_low < atoi(th[x].trs_warning)) {
         strcpy(mea[x].elapsed_low_html_color, HTML_GREEN);
         }
      if ((mea[x].elapsed_low > atoi(th[x].trs_warning)) && (mea[x].elapsed_low < atoi(th[x].trs_error))){
         strcpy(mea[x].elapsed_low_html_color, HTML_YELLOW);
         }
      if (mea[x].elapsed_low > atoi(th[x].trs_error)) {
         strcpy(mea[x].elapsed_low_html_color, HTML_RED);
         }
      // elapsed high
      if (mea[x].elapsed_high < atoi(th[x].trs_warning)) {
         strcpy(mea[x].elapsed_high_html_color, HTML_GREEN);
         }
      if ((mea[x].elapsed_high > atoi(th[x].trs_warning)) && (mea[x].elapsed_high < atoi(th[x].trs_error))){
         strcpy(mea[x].elapsed_high_html_color, HTML_YELLOW);
         }
      if (mea[x].elapsed_high > atoi(th[x].trs_error)) {
         strcpy(mea[x].elapsed_high_html_color, HTML_RED);
         }
      // Avg 10
      if (mea[x].elapsed_gns10 < atoi(th[x].trs_warning)) {
         strcpy(mea[x].elapsed_gns10_html_color, HTML_GREEN);
         }
      if ((mea[x].elapsed_gns10 > atoi(th[x].trs_warning)) && (mea[x].elapsed_gns10 < atoi(th[x].trs_error))){
         strcpy(mea[x].elapsed_gns10_html_color, HTML_YELLOW);
         }
      if (mea[x].elapsed_gns10 > atoi(th[x].trs_error)) {
         strcpy(mea[x].elapsed_gns10_html_color, HTML_RED);
         }
      // Avg 100
      if (mea[x].elapsed_gns100< atoi(th[x].trs_warning)) {
         strcpy(mea[x].elapsed_gns100_html_color, HTML_GREEN);
         }
      if ((mea[x].elapsed_gns100 > atoi(th[x].trs_warning)) && (mea[x].elapsed_gns100 < atoi(th[x].trs_error))){
         strcpy(mea[x].elapsed_gns100_html_color, HTML_YELLOW);
         }
      if (mea[x].elapsed_gns100 > atoi(th[x].trs_error)) {
         strcpy(mea[x].elapsed_gns100_html_color, HTML_RED);
         }
      // Avg 1000
      if (mea[x].elapsed_gns1000< atoi(th[x].trs_warning)) {
         strcpy(mea[x].elapsed_gns1000_html_color, HTML_GREEN);
         }
      if ((mea[x].elapsed_gns1000 > atoi(th[x].trs_warning)) && (mea[x].elapsed_gns1000 < atoi(th[x].trs_error))){
         strcpy(mea[x].elapsed_gns1000_html_color, HTML_YELLOW);
         }
      if (mea[x].elapsed_gns1000 > atoi(th[x].trs_error)) {
         strcpy(mea[x].elapsed_gns1000_html_color, HTML_RED);
         }
      // Color of returncodes
      if (mea[x].last_returncode == 200) strcpy(mea[x].last_returncode_html_color, HTML_GREEN);
         else
         strcpy(mea[x].last_returncode_html_color, HTML_RED);

      } /* for */
   } /* compute_colors */