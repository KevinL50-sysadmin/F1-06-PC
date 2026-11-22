// Pruebas del bloque puro de los controles personalizados (src/f1_overrides.cpp, entre F1CONTROLES-PURO-INICIO y
// -FIN): reasignacion de botones y cuando se conduce. Uso: bash scripts/probar-controles.sh
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "controles-puro.inc"
static int fallos = 0, pruebas = 0;
static void comprobar(bool c, const char *nombre) { ++pruebas; if (!c) { ++fallos; std::printf("FALLO: %s\n", nombre); } }
// 0 cruz,1 cuadrado,2 circulo,3 l1,4 r1,5 l2,6 r2,7 triangulo (bit 4 del byte 3, presion en el byte 12)
static void base(uint8_t *b) { std::memset(b, 0, 32); b[0]=0; b[1]=0x73; b[2]=0xFF; b[3]=0xFF; for (int i=4;i<8;++i) b[i]=0x80; b[20]=1; }
static void pulsar(uint8_t *b, int bit, int presion, uint8_t p) { b[3] &= (uint8_t)~(1u<<bit); b[presion]=p; }
int main()
{
    uint8_t b[32], o[32];
    const int def[5] = {0,1,6,5,4};
    // Defecto = identidad, con varios estados (todos los bits y presiones)
    for (int k = 0; k < 256; ++k) {
        base(b); b[2]=(uint8_t)(k*37); b[3]=(uint8_t)k; for (int i=8;i<20;++i) b[i]=(uint8_t)(k*7+i*13);
        std::memcpy(o,b,32); controles::reasignar(b, def);
        comprobar(std::memcmp(o,b,32)==0, "defecto: el informe no cambia");
    }
    // S1/S3: ACELERAR=r2, SUBIR MARCHA=cruz (intercambio)
    { const int e[5]={6,1,0,5,4};
      base(b); pulsar(b,1,19,120); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<6))==0 && b[14]==120, "r2 fisico -> cruz del juego con su presion");
      comprobar((b[3]&(1u<<1))!=0 && b[19]==0, "r2 del juego suelto (cruz fisica suelta)");
      base(b); pulsar(b,6,14,255); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<1))==0 && b[19]==255, "cruz fisica -> r2 del juego (subir marcha)");
      comprobar((b[3]&(1u<<6))!=0 && b[14]==0, "cruz del juego suelta");
      base(b); b[2]=0x12; b[4]=1;b[5]=2;b[6]=3;b[7]=4; for(int i=8;i<13;++i) b[i]=(uint8_t)(i*3); pulsar(b,1,19,50);
      std::memcpy(o,b,32); controles::reasignar(b,e);
      comprobar(std::memcmp(o,b,3)==0 && std::memcmp(o+4,b+4,9)==0 && std::memcmp(o+20,b+20,12)==0, "bytes ajenos intactos"); }
    // S3: ACELERAR=cuadrado, FRENAR=cruz
    { const int e[5]={1,0,6,5,4};
      base(b); pulsar(b,7,15,200); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<6))==0 && b[14]==200 && (b[3]&(1u<<7))!=0 && b[15]==0, "cuadrado fisico acelera con presion");
      base(b); pulsar(b,7,15,200); pulsar(b,6,14,30); controles::reasignar(b,e);
      comprobar((b[3]&0xC0)==0 && b[14]==200 && b[15]==30, "ambos: permutacion, no encadenado"); }
    // ○ asignado: MIRAR ATRAS=circulo
    { const int e[5]={0,1,6,5,2};
      base(b); pulsar(b,5,13,90); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<3))==0 && b[18]==90, "circulo fisico -> r1 del juego con presion");
      comprobar((b[3]&(1u<<5))!=0 && b[13]==0, "circulo del juego suelto si esta asignado");
      base(b); pulsar(b,3,18,60); controles::reasignar(b,e);
      comprobar(b[3]==0xFF && b[18]==0, "r1 fisico sin accion: nada"); }
    // L1 asignado: SUBIR MARCHA=l1
    { const int e[5]={0,1,3,5,4};
      base(b); pulsar(b,2,16,77); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<1))==0 && b[19]==77 && (b[3]&(1u<<2))!=0 && b[16]==0, "l1 fisico sube marcha; l1 del juego suelto");
      base(b); pulsar(b,1,19,99); controles::reasignar(b,e);
      comprobar(b[3]==0xFF && b[19]==0, "r2 fisico sin accion: nada"); }
    // ○ no asignado pasa
    { const int e[5]={6,1,0,5,4};
      base(b); pulsar(b,5,13,44); pulsar(b,2,16,45); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<5))==0 && b[13]==44 && (b[3]&(1u<<2))==0 && b[16]==45, "circulo y l1 sin asignar pasan"); }
    // Triangulo sin asignar pasa tal cual (es la camara del juego)
    { const int e[5]={6,1,0,5,4};
      base(b); pulsar(b,4,12,66); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<4))==0 && b[12]==66, "triangulo sin asignar pasa con su presion");
      base(b); pulsar(b,4,12,66); std::memcpy(o,b,32); controles::reasignar(b,def);
      comprobar(std::memcmp(o,b,32)==0, "defecto: triangulo pulsado, el informe no cambia"); }
    // controles-en-carrera T3 (S2): MIRAR ATRAS=triangulo intercambia con R1 (la camara no se pierde)
    { const int e[5]={0,1,6,5,7};
      base(b); pulsar(b,4,12,88); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<3))==0 && b[18]==88, "triangulo fisico -> r1 del juego (mirar atras) con presion");
      comprobar((b[3]&(1u<<4))!=0 && b[12]==0, "triangulo del juego suelto al pulsar triangulo fisico");
      base(b); pulsar(b,3,18,70); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<4))==0 && b[12]==70, "r1 fisico -> triangulo del juego (camara) con presion");
      comprobar((b[3]&(1u<<3))!=0 && b[18]==0, "r1 del juego suelto al pulsar r1 fisico");
      base(b); pulsar(b,4,12,88); pulsar(b,3,18,70); controles::reasignar(b,e);
      comprobar((b[3]&0x18)==0 && b[18]==88 && b[12]==70, "triangulo y r1 a la vez: permutacion");
      base(b); pulsar(b,6,14,200); pulsar(b,5,13,40); pulsar(b,2,16,41); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<6))==0 && b[14]==200 && (b[3]&(1u<<5))==0 && b[13]==40 && (b[3]&(1u<<2))==0 && b[16]==41,
                "mirar atras=triangulo: cruz, circulo y l1 intactos"); }
    // Esquema de la issue #2: ACELERAR r2, FRENAR l2, SUBIR cuadrado, BAJAR cruz, MIRAR ATRAS triangulo
    { const int e[5]={6,5,1,0,7};
      base(b); pulsar(b,1,19,150); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<6))==0 && b[14]==150 && (b[3]&(1u<<1))!=0 && b[19]==0, "issue #2: r2 acelera con presion");
      base(b); pulsar(b,0,17,80); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<7))==0 && b[15]==80 && (b[3]&(1u<<0))!=0 && b[17]==0, "issue #2: l2 frena con presion");
      base(b); pulsar(b,7,15,255); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<1))==0 && (b[3]&(1u<<7))!=0, "issue #2: cuadrado sube marcha");
      base(b); pulsar(b,6,14,255); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<0))==0 && (b[3]&(1u<<6))!=0, "issue #2: cruz baja marcha");
      base(b); pulsar(b,4,12,90); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<3))==0 && b[18]==90 && (b[3]&(1u<<4))!=0, "issue #2: triangulo mira atras");
      base(b); pulsar(b,3,18,91); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<4))==0 && b[12]==91 && (b[3]&(1u<<3))!=0, "issue #2: r1 fisico hace de camara"); }
    // Triangulo con cadena: MIRAR ATRAS=triangulo y ACELERAR=r1 -> la camara va a la cruz fisica (la que queda libre)
    { const int e[5]={4,1,6,5,7};
      base(b); pulsar(b,6,14,33); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<4))==0 && b[12]==33 && (b[3]&(1u<<6))!=0 && b[14]==0, "cadena: cruz fisica -> triangulo del juego");
      base(b); pulsar(b,3,18,34); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<6))==0 && b[14]==34 && (b[3]&(1u<<4))!=0 && b[12]==0, "cadena: r1 fisico acelera, sin camara"); }
    // Circulo y triangulo asignados: circulo del juego suelto, triangulo recibe el boton libre
    { const int e[5]={0,1,6,2,7};
      base(b); pulsar(b,5,13,20); pulsar(b,4,12,21); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<0))==0 && b[17]==20 && (b[3]&(1u<<3))==0 && b[18]==21 && (b[3]&0x30)==0x30, "circulo baja, triangulo mira atras");
      base(b); pulsar(b,3,18,22); pulsar(b,0,17,23); controles::reasignar(b,e);
      comprobar((b[3]&(1u<<4))==0 && b[12]==22 && (b[3]&(1u<<5))!=0 && b[13]==0 && (b[3]&0x09)==0x09, "r1 fisico camara; l2 fisico nada"); }
    // Eleccion invalida: sin cambios
    { const int e[5]={0,0,6,5,4}; const int f[5]={0,1,6,5,8};
      base(b); pulsar(b,7,15,10); std::memcpy(o,b,32); controles::reasignar(b,e); controles::reasignar(b,f);
      comprobar(std::memcmp(o,b,32)==0, "eleccion invalida: sin cambios"); }
    // Cuando se conduce (controles-en-carrera T2): valores medidos con PS2X_PAD_DEPURAR=1 en cada modo
    { using controles::Conduccion; using controles::activa;
      auto c = [](bool paso, uint32_t serv, uint32_t menu, uint32_t coche, uint32_t aviso) {
          Conduccion d; d.partida = true; d.pasoCarrera = paso; d.servidor = serv; d.menu = menu; d.coche = coche; d.aviso = aviso; return d; };
      comprobar(activa(c(true, 4, 0, 4, 0x25)), "carrera rapida en marcha: activa");
      comprobar(activa(c(false, 4, 0xFFFFFFFFu, 4, 0x25)), "fin de semana de GP en pista (paso 0, menu -1): activa");
      comprobar(activa(c(false, 4, 1, 4, 0x25)), "contrarreloj en pista (paso 0, menu 1): activa");
      comprobar(activa(c(false, 4, 0xFFFFFFFFu, 4, 0x16)), "GP con aviso informativo 0x16: activa");
      comprobar(!activa(c(true, 3, 0, 4, 0x25)), "pausa (servidor 3): inactiva");
      comprobar(!activa(c(false, 4, 0xFFFFFFFFu, 7, 0x12)), "garaje del GP (coche 7): inactiva");
      comprobar(!activa(c(true, 4, 0, 3, 0x11)), "parrilla de la carrera rapida (coche 3): inactiva");
      comprobar(!activa(c(true, 4, 0, 1, 0x25)), "tras CONTINUE de retirado (coche 1): inactiva");
      comprobar(!activa(c(true, 4, 0, 4, 15)), "aviso de descalificado que espera pulsacion: inactiva");
      Conduccion r = c(true, 4, 0, 4, 0x25); r.repeticion = 1; comprobar(!activa(r), "repeticion: inactiva");
      Conduccion f = c(true, 4, 0, 4, 0x25); f.foto = true; comprobar(!activa(f), "modo foto: inactiva");
      Conduccion p = c(true, 4, 0, 4, 0x25); p.partida = false; comprobar(!activa(p), "sin partida: inactiva"); }
    std::printf("pruebas=%d fallos=%d\n", pruebas, fallos);
    return fallos ? 1 : 0;
}
