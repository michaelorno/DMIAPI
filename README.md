# DMIAPI
dmiapi dokumentation
Version 1.00 22.10.2021

Navn:
	dmiapi - DMI API Overvågningsprobe
        2020-2021 Michael Ørnø

Sourcecode: https://github.com/michaelorno/DMIAPI.git

Synopsis
	./dmiapi [konfigfil]
	
Beskrivelse:
	dmiapi måler aktuelt svartider mod DMI's åbne data på fire API'er (metObs, oceanObs, lightObs & climateObs).
	Svartiden måles ved at sende en 'GET' forspørgsel til API'ets gateway og læse det svar,
	der returneres. Den tid der løber mellem afsendelse og modtagelse måles som transaktionens
	svartid. Denne svartid kan betegnes som den brugeroplevede svartid mod DMI's API'er. 
	Den svartid er summen af svartiden på API'et og netværkstiden fra og til klienten.

	Programmet viser en målekonsol på tty, sizet til 132*24. Her vises resultaterne af den seneste måling.

        Programmet danner en html-side med konsoloutput, der kan bruges til visning af konsolen på en browser.

        Programmet opsamler statistik på svartider på de fire API’er og gemmer i en log-fil pr døgn.

        Programmet skriver alarmer i operativsystemets syslog pr 10. måling baseret på de svartidsgrænseværdier 
        der vælges. Disse alarmer kan hentes fra operativsystemets syslog af standard værktøjer til brug for 
        overvågningsmonitors. Disse alarmer skrives tilsvarende i en lokal log.

        Programmet gemmer hver transaktions gravitee transaktionsid og svartid i en logfil.

        For hvert API skal der oprettes en adgang og en adgangsnøgle (apikey) på dmiapi.govcloud.dk.

Funktion:
	På metObs forespøges der på seneste målte temperatur i 2 m. højde (&dry_temp) på 16 
	forskellige målestationer i fast rækkefølge:

		Station_id:	Navn:
		06041 		Skagen fyr
		06079 		Anholt havn
		06081 		Blaavandshuk fyr
		06183 		Drogden fyr
		06193 		Hammerodde fyr
		06169 		Gniben 
		06119 		Kegnaes fyr
		06188 		Sjaelsmark
		06074 		Aarhus syd
		06184 		DMI
		06149 		Gedser
		06096 		Roemoe/Juvre
		06168 		Nakkehoved fyr
		06068 		Isenvad
		04320 		Danmarkshavn
		04250 		Nuuk
		04220 		Aasiaat

	Den request der sendes til metObs API'et har følgende format:
	"GET /metObs/v1/observation?parameterId=temp_dry"&latest-10-minutes&api-key=[key]
	&stationId=[Station_id] HTTP/1.1\r\nHost:dmigw.govcloud.dk\r\nAccept: application/json\r\n\r\n"
        Data (&temp_dry) vises på monitoren til alm. underholdning.

	På oceanObs forespøges der på seneste målte vandstand (&sealev_dvr) på 16 
	forskellige målestationer i fast rækkefølge:
		Station_id	Navn:
		28548 		Bagenkop havn
		27084 		Ballen havn
		30357 		Drogden fyr
		25149 		Esbjerg havn
		20101 		Frederikshavn I
		31616 		Gedser havn I
		29002 		Havnebyen/Sj.od.
		29038 		Holbæk havn I
		29393 		Korsør havn I
		30336 		Kbh. havn
		30407 		Roskilde havn I
		31573 		Roedbyhavn I
		32048 		Tejn
		30202 		Vedbaek I
		22331 		Aarhus havn I
		26359 		Vidaa/Hoejer
		30017 		Hornbaek

	Den request der sendes til oceanObs API'et har følgende format:
	"GET /v2/oceanObs/collections/observation/items?parameterId=sealev_dvr&period=latest&api-key=[key]
	&stationId=[Station_id] HTTP/1.1\r\nHost:dmigw.govcloud.dk\r\nAccept: application/json\r\n\r\n"
        Data (&sealev_dvr) vises på monitor til almindelig underholdning.

	På lightObs sendes en standardrequest, der spørger på tilgængelighed af målestationer.
	Den request der sendes til oceanObs API'et har følgende format:
	"GET /v2/lightningdata/collections/observation/items?limit=1&api-key=[key]
	 HTTP/1.1\r\nHost:dmigw.govcloud.dk\r\nAccept: application/json\r\n\r\n"
        Seneste observation vises på monitoren.

Parameteropsætning:
	        [USERID] identifier (string without whitespaces)
                [IPHOST] symbolsk adresse på gateway (https://dmigw.govcloud.dk)
                [FREQ] wait n seconds between issue of request-triplet (int)
                [WWW-PATH] path for index.html-file (string) 
		[METOBSKEY] api-key (string) - obtain key at dmi.dk
                [OCEANOBSKEY] api-key (string) - obtain key at dmi.dk
                [LIGHTOBSKEY] api-key (string) - obtain key at dmi.dk
                [CLIMATEKEY] api-key (string) - obtain key at dmi.dk
                [METOBSAPI_THRESHOLD_WARNING] threshold for issue of warning i syslog in ms (int)
                [METOBSAPI_THRESHOLD_ERROR] threshold for issue of error in syslog in  ms  (int)
                [OCEANOBSAPI_THRESHOLD_WARNING] threshold for issue of warning i syslog in ms (int)
                [OCEANOBSAPI_THRESHOLD_ERROR] threshold for issue of error in syslog in  ms  (int)
                [LIGHTOBSAPI_THRESHOLD_WARNING] threshold for issue of warning i syslog in ms (int)
                [LIGHTOBSAPI_THRESHOLD_ERROR] threshold for issue of error in syslog in  ms  (int)
                [CLIMATEOBSAPI_THRESHOLD_WARNING] threshold for issue of warning i syslog in ms (int)
                [CLIMATEOBSAPI_THRESHOLD_ERROR] threshold for issue of error in syslog in  ms  (int)
                [SILENT] 0|1  (0=slient, 1=console output))
                (*) Remark: [PARAMETER] and value must be separated by a white space
                Bemærk: Der skal være et blanktegn mellem parameternavn og værdi.

Filformater:
	Transaktionslog:
	Der dannes en ny fil hvert døgn kl 00.00 GMT med filnavn ÅÅÅÅ-MM-DD_dmiapi.trans
        Der skrives en linje ved hver transaktion der afsendes.
        Format: [Dato/tid], [API_id], [http_returkode], [Transaktionskode], [Svartid]
	hvor: 
		[Dato tid] er det tidspunkt programmet skriver linjen i loggen - GMT
		[API_id] er [0|1|2|3] hvor 0=metObs, 1=oceanObs, 2=lightObs, 3=climateObs
		[http_returkode] er den returkode gateway'ens webserver har givet (eks:200=ok)
		[Transaktionskode] er Gravitee-io transaktionskoden fra API'et
		[Svartid] er i millisek. set fra klienten.
	Eksempel:
		16 Dec 2020 23:03:33 GMT,0,200,c5292e04-9561-4ea8-a92e-049561eea890,   36.08

	Statistiklog:
        Statistikloggen bruges til at opsamle performancestatistik baseret på gennemsnittet af de 10, 100 eller 1000 seneste målinger.
	Der dannes en ny fil hvert døgn kl 00.00 GMT med filnavn ÅÅÅÅ-MM-DD_dmiapi.stat
	Der skrives en linje for hver 10 transaktion afsendt til de tre API'er.
	Format: [Dato/tid], [Stat_kode], [Gns. svartid], [Svartid low], [Svartid high]
	hvor:
		[Dato/tid] er det tidspunkt programmet skiver linjen i loggen - GMT
                [Stat_kode] er “m”|”o”|”l”<“10”|”100”|”1000”>, hvor
			1.  ciffer er API_id, hvor “m”=metObs,”o”=oceanObs,”l”=lightObs,"c"=climateObs
			2-“n” ciffer er “10”|”100”|”1000” - måling efter hhv. 10,100,1000 transaktioner
		
		[Gns. svartid] er den gennemsnitlige svartid i millisekunder for de seneste 10, 100 eller 1000 transaktioner
		[Svartid low] er den gennemsnitlige svartid i millisekunder for de seneste 10, 100 eller 1000 transaktioner
		[Svartid high] er den gennemsnitlige svartid i millisekunder for de seneste 10, 100 eller 1000 transaktioner
        Eksempel:
                12 Jan 2021 09:48:54 GMT,  m10,   38.98,   23.04,   54.92
	
	
	Overvågningslog:
        Overvågningsloggen bruges til overvågning af performance. For hver 10 transaktion beregnes den gennemsnitlige svartid.
        Hvis denne svartid er mindre end [[API]TREASHOLD_WARNING] skrives en linje i loggen af typen NOTICE.
        Hvis svartiden er større end [[API]TREASHOLD_WARNING] men mindre end [TREASHOLD_ERROR] skrives en linje af type WARNING.
        Hvis svartiden er større end [[API]TREASHOLD_ERROR] skrives en linje af typen ERROR.

	Der skrives en linje for hver 10 afsendt til de tre API'er.
        Der skrives parallelt i operativsystemets syslog og i en selvstændig logfil. Log-records der sendes i syslog, kan hentes ud af samme
        til en overvågningsmonitor. Den selvstændige logfil kan bruges til en primitiv overvågningskonsol eks med “tail -f logfilnavn”.

        Der dannes en ny fil hvert døgn kl 00.00 GMT med filnavn ÅÅÅÅ-MM-DD_dmiapi.log
	Format: [Dato/tid] [Message]
	hvor:
		[Dato/tid] er det tidspunkt programmet skriver linjen i loggen - GMT
                [Message] “DMIAPI”[SERVERITY_CODE]: “NOTICE|WARNING|ERROR” [API_ID] “avg10=“ [Gns.10 svartid]
                Hvor:
                     [SEVERITY_CODE] er “1”=NOTICE, “2”=WARNING eller “3”=ERROR
                     [API_ID] = “metObs”|”oceanObs”|”lightObs”|"climateObs"
                     [Gns.10 svartid] er gennemsnittet af de ti seneste målinger i millisekunder
