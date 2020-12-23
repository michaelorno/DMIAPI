//	dmiapi.c 	20122020/MOE
//	Build: cc dmiapi.c -o dmiapi
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
//      	[THRESHOLD_WARNING] threshold for issue of warning i syslog in ms (int)
//      	[THRESHOLD_ERROR] threshold for issue of error in syslog in  ms  (int)
//      	[SILENT] 0|1  (0=slient, 1=console output))
//      	(*) Remark: [Parameter Id] and value must be separated by a white space
//
//	Dokumentation: dmiapi.txt
//
//	Versionshistorie:
//		0.91 Initial version
//		0.92 All 3 API's added
//		0.93 Syslog implemented
//		0.94 Code tidy-up and publish on github
//
//	To-do:
//		check memory-leaks
//		check alle [] for overløb -> trigger v.1.00
//		hent gravetee.io translog


#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <syslog.h>
#ifdef __linux__ 
   # include <time.h>     // Linux
#elif defined __APPLE__
   # include <sys/time.h> // MacOS X
#endif
#include <unistd.h>

#define	TCPIPDEBUG 0		// if !=0 then debugmsg to tty
#define	HTTPLOGGING 1		// if !=0 then output http send/receive on tty
#define VERSION "0.94"

// File definitions
FILE * http_debug_file;	// HTTP debugging
FILE * http_out;	// Write index.html-file
FILE * syslog_out;	// Local copy of messages to syslog
FILE * statlog_out;	// Statistics-log
FILE * translog_out;	// Transaction-log
FILE * config_file;	// Configuration-file

// Variables for TCPIP
int hSocket, read_size;
struct sockaddr_in server;

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
   char line[85];
   } screen[24];

// Statistics
struct data_record{
   char data[30];
   } observation[3];

struct http_resp_record{
   int http_204;
   int http_other;
   } http_resp[3];

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
   } mea[3];

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
  "29038", "Holbæk havn I   ",
  "29393", "Korsør havn I   ",
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
   char iphost[80];
   char freq[80];
   char wwwpath[80];
   char trs_warning[80];
   char trs_error[80];
   char silent[80];

// Function prototypes

// TCPIP
short SocketCreate(void);
int SocketConnect(int hSocket);
int SocketSend(int hSocket,char* Rqst,short lenRqst);
int SocketReceive(int hSocket,char* Rsp,short RvcSize);

// Logs
void write_syslog(const char* msg, int pri);
void write_statlog(char* trans_type, char* trans_date, double trans_tid, double low, double high);
void write_translog(char* trans_date, int api_id, char* http_code, char* trans_id, double trans_tid);

// Output
void view_console();
void http_output();

// Init & and functions
int goodbye(int status_code);
void read_config(char* config_filename);

// Misc.
float timedifference_msec(struct timeval t0, struct timeval t1);

// API functions
void api_request(char* api, char* station_id);

int main(int argc, char *argv[]){
   int g10, g100, g1000, x;
   char c, syslog_txt[80];

   http_debug_file = fopen ("dmiapi_http.log", "w");
   write_syslog("Monitor started", 0);

   // Read configuration parameters
   strcpy(config_filename,argv[1]); // filename
   read_config(config_filename);

   // Start time
   start_time = time(NULL);
   if (start_time == ((time_t)-1)) {
       write_syslog("Failure to obtain the current time", 3);
       goodbye(3);
       }
   strcpy(start_c_time_string, ctime(&start_time));
   start_c_time_string[strlen(start_c_time_string)-1]=0;

   // Initialize
   for (x=0; x<=2; x++){
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

   // Create socket
   hSocket = SocketCreate();
   if (hSocket == -1) {
      write_syslog("Could not create socket",3);
      goodbye(3);
      }
   if (TCPIPDEBUG) write_syslog("TCPIP MAIN: Socket created",0);

   // Establish connection to server
   if (SocketConnect(hSocket) < 0) {
      write_syslog("Forbindelse til server fejler",3);
      goodbye(3);
      } 
   if (TCPIPDEBUG) write_syslog("TCPIP MAIN: Connected to server", 0);

   while(1){

      // Do 3 requests
      for (x=0; x<=2; x++){
         if (x == 0) api_request("metObsAPI", stations_liste[stations_count].kode);
         if (x == 1) api_request("oceanObsAPI", kyst_stations_liste[stations_count].kode);
         if (x == 2) api_request("lightObsAPI", kyst_stations_liste[stations_count].kode);

         mea[x].requests++;
         mea[x].elapsed_sum10 = mea[x].elapsed_sum10 + mea[x].elapsed;
         mea[x].elapsed_sum100 = mea[x].elapsed_sum100 + mea[x].elapsed;
         mea[x].elapsed_sum1000 = mea[x].elapsed_sum1000 + mea[x].elapsed;
         if (mea[x].elapsed > mea[x].elapsed_high) mea[x].elapsed_high = mea[x].elapsed;
         if (mea[x].elapsed < mea[x].elapsed_low) mea[x].elapsed_low = mea[x].elapsed;
         } /* for */

      // Calculate
      if (g10 == 10) {
         for (x=0; x <= 2; x++){
            mea[x].elapsed_gns10 = mea[x].elapsed_sum10 / 10;
            mea[x].elapsed_sum10 = 0;

            if (x == 0) snprintf(syslog_txt,79,"metObs avg10= %8.2f", mea[x].elapsed_gns10);
            if (x == 1) snprintf(syslog_txt,79,"oceanObs avg10= %8.2f", mea[x].elapsed_gns10);
            if (x == 2) snprintf(syslog_txt,79,"lightObs avg10= %8.2f", mea[x].elapsed_gns10);
            if (mea[x].elapsed_gns10 <= atoi(trs_warning))
               write_syslog(syslog_txt, 1);
            else if (mea[x].elapsed_gns10 > atoi(trs_warning) && mea[x].elapsed_gns10 < atoi(trs_error))
               write_syslog(syslog_txt, 2);
            else if (mea[x].elapsed_gns10 > atoi(trs_error))
               write_syslog(syslog_txt, 3);
            if (x == 0) write_statlog("m10",trans_dato, mea[x].elapsed_gns10, mea[x].elapsed_low, mea[x].elapsed_high);
            if (x == 1) write_statlog("o10",trans_dato, mea[x].elapsed_gns10, mea[x].elapsed_low, mea[x].elapsed_high);
            if (x == 2) write_statlog("l10",trans_dato, mea[x].elapsed_gns10, mea[x].elapsed_low, mea[x].elapsed_high);
            } /* for */
         g10 = 0;
      } /* == 10 */

      if (g100 == 100) {
         for (x=0; x <= 2; x++){
            mea[x].elapsed_gns100 = mea[x].elapsed_sum100 / 100;
            mea[x].elapsed_sum100 = 0;
            if (x == 0) write_statlog("m100",trans_dato, mea[x].elapsed_gns100, mea[x].elapsed_low, mea[x].elapsed_high);
            if (x == 1) write_statlog("o100",trans_dato, mea[x].elapsed_gns100, mea[x].elapsed_low, mea[x].elapsed_high);
            if (x == 2) write_statlog("l100",trans_dato, mea[x].elapsed_gns100, mea[x].elapsed_low, mea[x].elapsed_high);
            } /* for */
         g100 = 0;
         } /* == 100 */

      if (g1000 == 1000) {
         for (x=0; x <= 2; x++){
            mea[x].elapsed_gns1000 = mea[x].elapsed_sum1000 / 1000;
            mea[x].elapsed_sum1000 = 0;
            if (x == 0) write_statlog("m1000",trans_dato, mea[x].elapsed_gns1000, mea[x].elapsed_low, mea[x].elapsed_high);
            if (x == 1) write_statlog("o1000",trans_dato, mea[x].elapsed_gns1000, mea[x].elapsed_low, mea[x].elapsed_high);
            if (x == 2) write_statlog("l1000",trans_dato, mea[x].elapsed_gns1000, mea[x].elapsed_low, mea[x].elapsed_high);

            // Reset low/high
            mea[x].elapsed_low=1000;
            mea[x].elapsed_high=0;
            }
         g1000 = 0;

         } /* == 1000 */

      current_time=time(NULL);

      g10++;
      g100++;
      g1000++;

      // View console & do http output
      view_console();
      http_output();

      stations_count++;
      if (stations_count == 17) stations_count=0;

      sleep(atoi(freq));
   } /* while */

   close(hSocket);
   shutdown(hSocket,0);
   shutdown(hSocket,1);
   shutdown(hSocket,2);

   return 0;
   } /* main */

// Get and interpret data
void api_request(char* api, char* station_id){
   int x, y, http_ok;
   int api_type = 0; //0=metObs,1=oceanObs,2=lightObs
   char data_temp[40] = {0};
   char SendToServer[2000] = {0};
   char server_reply[2000] = {0};
   char trans_data[2000] = {0};
   char trans_data2[2000] = {0};;
   char data[2000] = {0};
   char http_ret_code[5] = {0};

   SendToServer[0]=0;
   if (strcmp(api,"metObsAPI") == 0){ 
      api_type=0;
      strcpy(SendToServer,"GET /metObs/v1/observation?parameterId=temp_dry");
      strcat(SendToServer,"&latest-10-minutes&api-key=");
      strcat(SendToServer,key_metobs);
      strcat(SendToServer,"&stationId=");
      strcat(SendToServer,station_id);
      strcat(SendToServer," HTTP/1.1\r\nHost:dmigw.govcloud.dk\r\nAccept: application/json\r\n\r\n");
      }
   if (strcmp(api,"oceanObsAPI") == 0){
      api_type=1;
      strcpy(SendToServer,"GET /v2/oceanObs/collections/observation/items?parameterId=sealev_dvr&period=latest&api-key=");
      strcat(SendToServer,key_oceanobs);
      strcat(SendToServer,"&stationId=");
      strcat(SendToServer,station_id);
      strcat(SendToServer," HTTP/1.1\r\nHost:dmigw.govcloud.dk\r\nAccept: application/json\r\n\r\n");
      }
   if (strcmp(api,"lightObsAPI") == 0){
      api_type=2;
      strcpy(SendToServer,"GET /v2/lightningdata/collections/observation/items?limit=1&api-key=");
      strcat(SendToServer,key_lightobs);
      strcat(SendToServer," HTTP/1.1\r\nHost:dmigw.govcloud.dk\r\nAccept: application/json\r\n\r\n");
      }

   // Send data to server
   if (HTTPLOGGING) fprintf(http_debug_file,"[TCPIP Send]%s[EOS]\n", SendToServer);
   gettimeofday(&t0, 0); // Measure t0
   SocketSend(hSocket, SendToServer, strlen(SendToServer));

   // Data recieved from server
   read_size = SocketReceive(hSocket, server_reply, 1999);
   gettimeofday(&t1, 0); // Measure t1
   if (api_type == 0) mea[0].elapsed = timedifference_msec(t0, t1);
   if (api_type == 1) mea[1].elapsed = timedifference_msec(t0, t1);
   if (api_type == 2) mea[2].elapsed = timedifference_msec(t0, t1);
   if (HTTPLOGGING) fprintf(http_debug_file,"[TCPIP Received]%s[EOS]\n",server_reply);
   
   // Decode HTTP-returncode
   http_ok = 99;
   if (strlen(server_reply) > 12){
      http_ret_code[0] = server_reply[9];
      http_ret_code[1] = server_reply[10];
      http_ret_code[2] = server_reply[11];
      http_ret_code[3] = 0;
      if (strcmp(http_ret_code,"200") == 0){
         http_ok = 0;
         } else {
         if (strcmp(http_ret_code,"204") == 0){
            http_ok = 204;
            if (api_type == 0){
               http_resp[0].http_204++;
               strcpy(observation[0].data,"No data");
               write_syslog("HTTP 204 modtaget fra metObs",2);
               }
            if (api_type == 1){
               http_resp[1].http_204++;
               strcpy(observation[1].data,"No data");
               write_syslog("HTTP 204 modtaget fra oceanObs",2);
               }
            if (api_type == 2){
               http_resp[2].http_204++;
               strcpy(observation[2].data,"No data");
               write_syslog("HTTP 204 modtaget fra lightObs",2);
               }
            } else {
            http_ok = 1;
            if (api_type == 0){
               http_resp[0].http_other++;
               strcpy(observation[0].data,"No data");
               write_syslog("HTTP <> 200/204 modtaget fra metObs",2);
               }
            if (api_type == 1){
               http_resp[1].http_other++;
               strcpy(observation[1].data,"No data");
               write_syslog("HTTP <> 200/204 modtaget fra oceanObs",2);
               }
            if (api_type == 2){
               http_resp[2].http_other++;
               strcpy(observation[2].data,"No data");
               write_syslog("HTTP <> 200+204 modtaget fra lightObs",2);
               }
            }
         }
      } /* if */

   strcpy(trans_data,"");
   strcpy(trans_data2,"No Transactioncode");
   if (http_ok == 0 ) { 		// returkode = 200 
      // Decode transactioncode
     if (strstr(server_reply,"x-gravitee-transaction-id:") != NULL)  strcpy(trans_data,strstr(server_reply,"x-gravitee-transaction-id:"));
      if (strlen(trans_data) > 0){
         x=y=0;
         while (trans_data[x] != ':' && x < 1999)
            x++;
         x=x+2; 
         while(trans_data[x] != 10 && x < 1999){   
            trans_data2[y]=trans_data[x];
            y++;
            x++;
            }
            trans_data2[y-1]=0;
         } /* if */
      }  
   // Decode transactiondate
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
      

   write_translog(trans_dato, api_type, http_ret_code,trans_data2, timedifference_msec(t0, t1));

   // Decode data-string
   strcpy(data_temp,"");
   strcpy(data,"");
   if (api_type == 2){
       if (strstr(server_reply,"amp") != NULL) strcpy(data,strstr(server_reply,"amp"));
       } else {
       if (strstr(server_reply,"value") != NULL) strcpy(data,strstr(server_reply,"value"));
       } 
   if (strlen(data) > 0){
      x=y=0;
      while (data[x] != ':' && x < 40) x++;
      x++;
      while ((data[x] != '}' && data[x] != ',') && x < 18){
         data_temp[y] = data[x];
         x++;
         y++;
         }
      data_temp[y]=0;
      }  /*if (api..*/
   strcpy(observation[api_type].data, data_temp);
   } /* api_request */

// View console
void view_console(){
   int x;

   strcpy(screen[0].line, userid);
   strcpy(screen[1].line, "DMI API response monitoring [");
   strcat(screen[1].line, VERSION);
   strcat(screen[1].line, "]");
   snprintf(screen[2].line, 79, "System start time                 : %s", start_c_time_string);
   snprintf(screen[3].line, 79, "Latest measurement                : %s", ctime(&current_time));
   strcpy(screen[4].line, "metObsAPI");
   snprintf(screen[5].line, 79, "Latest datapoint                  : %s C (temp 2m) @ %s", observation[0].data, stations_liste[stations_count].navn);
   snprintf(screen[6].line, 79, "Resp.time latest trans.    (msec) : %8.2f", mea[0].elapsed);
   snprintf(screen[7].line, 79, "Resp.time low/high         (msec) : %8.2f / %8.2f", mea[0].elapsed_low, mea[0].elapsed_high);
   snprintf(screen[8].line, 79, "Resp.time avg. 10/100/1000 (msec) : %8.2f / %8.2f / %8.2f", mea[0].elapsed_gns10, mea[0].elapsed_gns100, mea[0].elapsed_gns1000);
   snprintf(screen[9].line, 79, "# req./ret=204/ret=other          : %8i / %8i / %8i", mea[0].requests, http_resp[0].http_204, http_resp[0].http_other);
   strcpy(screen[10].line," ");
   strcpy(screen[11].line,"oceanObsAPI");
   snprintf(screen[12].line, 79, "Latest datapoint                  : %s cm (sealevel DVR) @ %s", observation[1].data, kyst_stations_liste[stations_count].navn);
   snprintf(screen[13].line, 79, "Resp.time latest.trans     (msec) : %8.2f", mea[1].elapsed);
   snprintf(screen[14].line, 79, "Rest.time low/high         (msec) : %8.2f / %8.2f", mea[1].elapsed_low,mea[1].elapsed_high);
   snprintf(screen[15].line, 79, "Resp.time avg. 10/100/1000 (msec) : %8.2f / %8.2f / %8.2f", mea[1].elapsed_gns10, mea[1].elapsed_gns100, mea[1].elapsed_gns1000);
   snprintf(screen[16].line, 79, "# req./ret=204/ret=other          : %8i / %8i / %8i", mea[1].requests, http_resp[1].http_204, http_resp[1].http_other);
   strcpy(screen[17].line," ");
   strcpy(screen[18].line, "lightningObsApi");
   snprintf(screen[19].line, 79, "Latest datapoint                  : %s ampere", observation[2].data);
   snprintf(screen[20].line, 79, "Resp.time latest trans     (msec) : %8.2f", mea[2].elapsed);
   snprintf(screen[21].line, 79, "Resp.time low/high         (msec) : %8.2f / %8.2f", mea[2].elapsed_low, mea[2].elapsed_high);
   snprintf(screen[22].line, 79, "Resp.time avg. 10/100/1000 (msec) : %8.2f / %8.2f / %8.2f", mea[2].elapsed_gns10, mea[2].elapsed_gns100, mea[2].elapsed_gns1000);
   snprintf(screen[23].line, 79, "# req./ret=204/ret=other          : %8i / %8i / %8i", mea[2].requests, http_resp[2].http_204, http_resp[2].http_other);

   // View
   if (atoi(silent) == 1){
      printf("\e[1;1H\e[2J"); // Clear screen
      for (x=0;x<=23;x++)
        printf("%s\n",screen[x].line);
      } /* if */
   } /* view_console */

// Write html-page with console-output
void http_output(){
   int x;

   http_out = fopen("index.html", "w");
   fprintf(http_out,"<!DOCTYPE html><html>\n<head><style> body { font-family: 'Courier New', monospace; } </style></head><body><pre>\n"); 
   for (x=0;x<=23;x++)
      fprintf(http_out,"%s<br>\n",screen[x].line);
   fprintf(http_out,"\n</pre></body></html>\n");

   fclose(http_out);
   } /* http_output */

// Write translog-event
void write_translog(char* trans_date, int api_id, char* http_code, char* trans_id, double trans_tid){
   char name[40];

   // One file per day
   time(&file_current_time);
   today = localtime(&file_current_time);
   snprintf(name, 40, "%d-%d-%d_dmiapi.trans", today->tm_year+1900, today->tm_mon+1, today->tm_mday);

   translog_out = fopen(name, "a+");
   fprintf(translog_out,"%10s,%i,%s,%s,%8.2f\n",trans_date ,api_id, http_code,trans_id, trans_tid);
   fclose(translog_out);
   } /* write_translog */

// Write statlog-event
void write_statlog(char* trans_type, char* trans_date, double trans_tid, double low, double high){
   char name[40];

   // One file per day
   time(&file_current_time);
   today = localtime(&file_current_time);
   snprintf(name, 40, "%d-%d-%d_dmiapi.stat", today->tm_year+1900, today->tm_mon+1, today->tm_mday);

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
   snprintf(name, 40, "%d-%d-%d_dmiapi.log", today->tm_year+1900, today->tm_mon+1, today->tm_mday);
   snprintf(log_time, 40, "%2d.%2d.%4d %2d:%2d:%2d", today->tm_mday, today->tm_mon+1,today->tm_year+1900, today->tm_hour, today->tm_min, today->tm_sec);

   syslog_out = fopen(name, "a+");
   if (pri == 0)
      fprintf(syslog_out,"%s DMIAPI[%i]: (INFO) %s\n",log_time, pri, msg);
   if (pri == 1)
      fprintf(syslog_out,"%s DMIAP[%i]: (NOTICE) %s\n",log_time, pri, msg);
   if (pri == 2) 
      fprintf(syslog_out,"%s DMIAPI[%i]: (WARNING) %s\n", log_time, pri, msg);
   if (pri == 3) 
      fprintf(syslog_out,"%s DMIAPI[%i]: (ERROR) %s\n", log_time, pri, msg);
   fclose(syslog_out);
 
   // Write in syslog
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
char parameter[80], value[80];
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
      if (strcmp(parameter, "[THRESHOLD_WARNING]") == 0) strcpy(trs_warning, value); else
      if (strcmp(parameter, "[THRESHOLD_ERROR]") == 0) strcpy(trs_error, value); else
      if (strcmp(parameter, "[SILENT]")== 0) strcpy(silent, value); 
      else {
         write_syslog("Unknown parameter in configurationfile - terminating", 3);
         printf("DMIAPI: Unknown parameter id in configurationfile: [%s]  - terminating", config_filename);
         goodbye(3); 
      }
   } /* while */
   fclose(config_file);

   // Check: Parameters contain some value
   if (strlen(wwwpath) == 0 || strlen(freq) == 0 || strlen(iphost) == 0 || strlen(key_metobs) == 0 || strlen(userid) == 0 ||
      strlen(key_oceanobs) == 0 || strlen(key_lightobs) == 0 || strlen(trs_warning) == 0 || strlen(trs_error) == 0 || strlen(trs_error) == 0) {
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

   // Check: 10 < [THRESHOLD_WARNING] < 10000
   if (atoi(trs_warning) < 10 || atoi(trs_warning) > 10000){
      printf("DMIAPI: [THRESHOLD_WARNING] must be between 10 and 10000 - terminating\n");
      write_syslog("[THRESHOLD_WARNING] must be between 10 and 10000 - terminating", 3);
      goodbye(3);
      } 

   // Check: 10 < [THRESHOLD_ERROR] < 10000
   if (atoi(trs_error) < 10 || atoi(trs_error) > 10000){
      printf("DMIAPI: [THRESHOLD_ERROR] must be between 10 and 10000 - terminating\n");
      write_syslog("[THRESHOLD_ERROR] must be between 10 and 10000 - terminating", 3);
      goodbye(3);
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
short SocketCreate(void) {
   short hSocket;
   hSocket = socket(AF_INET, SOCK_STREAM, 0);
   if (TCPIPDEBUG) 
      if (hSocket == 0) write_syslog("TCPIP SocketCreate: Socket created",0);

   return hSocket;
   } /* SocketCreate */

// Connect to server
int SocketConnect(int hSocket) {
   int iRetval=-1;
   int ServerPort = 80;
   struct sockaddr_in remote= {0};
   remote.sin_addr.s_addr = inet_addr(iphost); // From config-file
   remote.sin_family = AF_INET;
   remote.sin_port = htons(ServerPort);
   iRetval = connect(hSocket,(struct sockaddr *)&remote,sizeof(struct sockaddr_in));
   if (TCPIPDEBUG) 
      if (iRetval == 0) write_syslog("TCPIP SocketConnect: Socket connected",0);

   return iRetval;
   } /* SocketConnect */

// Send data to server
int SocketSend(int hSocket,char* Rqst,short lenRqst) {
   int shortRetval = -1;
   struct timeval tv;
   tv.tv_sec = 20;  /* 20 Sec timeout */
   tv.tv_usec = 0;
   if(setsockopt(hSocket,SOL_SOCKET,SO_SNDTIMEO,(char *)&tv,sizeof(tv)) < 0) {
      write_syslog("TCPIP: Socket send time Out",2);
      return -1;
      }
   shortRetval = send(hSocket, Rqst, lenRqst, 0);
   if (TCPIPDEBUG) write_syslog("TCPIP SockedSend:",0);
   return shortRetval;
   } /* SocketSend */

// Receive data from server
int SocketReceive(int hSocket,char* Rsp,short RvcSize) {
   int shortRetval = -1;
   struct timeval tv;
   tv.tv_sec = 20;  /* 20 Sec timeout */
   tv.tv_usec = 0;

   if(setsockopt(hSocket, SOL_SOCKET, SO_RCVTIMEO,(char *)&tv,sizeof(tv)) < 0) {
       write_syslog("TCPIP Socket receive time Out",2);
       return -1;
      }
   shortRetval = recv(hSocket, Rsp, RvcSize, 0);
   if (TCPIPDEBUG) write_syslog("TCPIP ReceiveRC:",0);
   Rsp[shortRetval]=0;
   return shortRetval;
   } /* SocketRecieve */
