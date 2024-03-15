In client se face un struct de subscriptii in care se retine socketul si topicurile
De asemenea se face un struct tcp_msg care va contine datele primite
de la server,in cazul nostru topicul, tipul de date transmis si payloadul.
In main se creeaza socketul TCP, se opreste algoritmul lui Nagle,
dupa care se conecteaza la server, dupa care se transmite id-ul clientului.
Se face adresa reutilizabila ca in laboratorul 7, dupa care incepe loop-ul.
Pentru primirea mai multor comenzi/date se foloseste poll, care verifica
ce fel de input se primeste.
Se primesc comenzile de subscribe, unsubscribe si exit.
Acestea trimit comanda si la server. Comanda va fi de forma
client_id+comanda+topicul+sf
Comanda de exit trimite id-ul+exit, dupa care se inchide socketul
Daca se primesc date de la server, acestea vor fi de forma structului
tcp_msg mentionat anterior, dupa care daca datele vor fi afisate in functie
de abonarile facute.

In server se initializeaza structul de clienti cu socketul,id-ul, un bool
pentru verificarea conexiunii, topicurile la care sunt clientii abonati si
mesajele trimise.
De asemenea se initializeaza structurile pentru mesajele udp si tcp care contin
topicul, tipul datelor si payloadul.

Functia de pow pentru conversie
Functia de initializare clienti
Functie pentru cautarea indexului clientilor in functie de id
Verificare daca id-ul clientului este luat
Functia de sters clientii dupa index-ul lor
Functia care adauga clienti
Functia care primeste un buf cu datele primite de la serverul udp si converteste
datele in functie de type-ul primit si care returneaza un tcp_msg
functia de handle_udp_message care trimite structul de tcp_msg la clienti

In main se creeaza si bind-uiesc socketurile udp si tcp,dupa care se fac
reutilizabile si se asculta pe socketul de tcp.
Se initializeaza un array pentru poll fd
In while dam handle la erori, conectare si deconectare
Daca se primeste o conexiune tcp se accepta si se face ne-blocanta si
socketul se adauga la array-ul de fd
Daca se priemste o conexiune udp, se face conversia si se trimit datele.
Altfel se updateaza client_socket-ul, se primeste intai client_id de fiecare
data.
Se da handle la erori
Dupa care se priemsc stringurile cu comenzi
Daca comanda este exit atunci se cauta indexul dupa id si se sterge clientul
Daca nu exista id_ul clientului in server, inseamnca ca este un client nou
si se initializeaza si anunta conexiunea la server.
Dupa aceea se separa comanda de restul stringului (topicul si sf-ul) si se
memoreaza la ce topicuri se aboneaza/dezaboneaza clientul

In final se inchid socketurile si se returneaza 0.

Ruland manual, abonarea si dezabonarea functioneaza corect pe multiplii clienti
Adica daca se ruleaza serverul si mai multi clienti,fiecare va primi mesaj
in functie de topicurile la care sunt abonati fiecare.
La fel si cu dezabonarea, daca se dezaboneaza de la un topic sunt 
afectati doar clientii care trimit comanda.