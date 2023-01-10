#ifndef _SIMULADOR_H_
#define _SIMULADOR_H_

typedef struct Rodada Rodada;
typedef struct Cliente Cliente;
typedef struct FilaEspera FilaEspera;
typedef enum tipo_evento TipoEvento;
typedef struct Evento Evento;

Rodada *criar_rodada();
void iniciar_fase_transiente();
void iniciar_nova_rodada();
Cliente *criar_cliente(Rodada *rodada);
FilaEspera *criar_fila();
void adicionar_cliente_fila(FilaEspera *fila, Cliente *cliente);
Cliente *prox_cliente_fila(FilaEspera *fila);
Evento *criar_evento(double momento,Cliente *cliente, TipoEvento tipo);
void agendar_evento(double momento, Cliente *cliente, TipoEvento tipo);
double amostra_exponencial(double taxa);
double variancia(double E_X, double *X);
long get_Nq1();
long get_N1();
long get_Nq2();
long get_N2();
void atualizar_E_Nq1();
void atualizar_E_N1();
void atualizar_E_Nq2();
void atualizar_E_N2();
void processar_evento_atual();
void processar_chegada_fila_1();
void processar_chegada_fila_2();
void processar_partida();
void processar_chegada_servico_1();
void processar_chegada_servico_2();
void interromper_servico_fila_2();
void encerrar_coleta(Rodada *rodada);
void gerar_intervalo_media(double media, double variancia, int n, double * intervalo_confianca);
void gerar_intervalo_variancia(double variancia, double precisao, double *intervalo_confianca);
double precisao_IC(double *intervalo_confianca);
void calcular_IC_rodadas();

#endif