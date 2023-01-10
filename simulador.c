#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>

#include "simulador.h"

/*----- Configurações do Simulador -----*/

#define SEED 358141284 // Semente da geração de números aleatórios

#define mu 1.0 // Taxa de serviço
#define rho 0.6 // Utilização do servidor

#define K 150ul // Número de coletas por rodada
#define K_t 300ul // Número de coletas da fase transiente
#define NUM_RODADAS 4000ul // Número de rodadas
#define p_variancia 0.044 // Precisão da variância para o número de rodadas fornecido
#define PRINT_RESULTADO_RODADA 0 // Se imprime ou não o resultado de cada rodada
                                 // (imprime se != 0)

// Cofigurações utilizadas nos resultados do relatório
// rho          0.2     0.4     0.6     0.8     0.9
// K            40ul    70ul    150ul   800ul   7000ul
// K_t          40ul    120ul   300ul   900ul   9000ul
// NUM_RODADAS  4000ul  4000ul  4000ul  4000ul  4000ul

/*--------------------------------------*/

// rho = 2*lambda*E[X] = 2*lambda/mu -> lambda = rho*mu/2 
#define lambda rho*mu/2.0

/**
 * Estrutura que contém as métricas coletadas de cada rodada
*/
struct Rodada
{
    double inicio; // momento em que a rodada começou
    Rodada *prox_rodada; // Ponteiro para a proxima rodada

    double E_W1; // E[W1]
    double E_T1; // E[T1]
    double E_Nq1; // E[Nq1]
    double E_N1; // E[N1]
    double E_W2; // E[W2]
    double E_T2; // E[T2]
    double E_Nq2; // E[Nq2]
    double E_N2; // E[N2]
    double V_W1; // V[W1]
    double V_W2; // V[W2]

    double W1[K]; // W1 de cada cliente, usado para calcular V(W1)
    double W2[K]; // W2 de cada cliente, usado para calcular V(W2)

    // Variáveis auxiliares no cáculo do número de pessoas na fila.
    // Armazenam o último instante em que cada número de pessoas na fila foi atualizado
    double ultima_atualizacao_E_Nq1;
    double ultima_atualizacao_E_N1;
    double ultima_atualizacao_E_Nq2;
    double ultima_atualizacao_E_N2;

    unsigned long num_chegadas; // Número de clientes que chegaram na rodada
    unsigned long num_partidas; // Número de clientes que chegaram na rodada e já partiram
};

/**
 * Estrutura que representa um cliente no sistema
*/
struct Cliente
{
    Rodada *rodada; // Rodada na qual o cliente chegou
    Cliente *prox_cliente; // Ponteiro para o cliente atrás dele na fila em que ele está

    unsigned long indice_rodada; // O i-ésimo cliente que chegar na rodada tem indice_rodada=i
    double chegada_estado_atual; // Instante de tempo em que o cliente chegou no estado atual (espera 1, serviço 1, espera 2 ou serviço 2)
    double chegada_fila_atual; // Instante de tempo em que o cliente chegou na fila atual (fila1 ou fila2)
};

/**
 * Estrutura que representa uma fila de clientes
*/
struct FilaEspera
{
    Cliente *primeiro_cliente; // Primeiro cliente na fila
    Cliente *ultimo_cliente; // Último cliente da fila
    unsigned long num_clientes; // Número de clientes na fila
};

// Tipos de eventos agendáveis
enum tipo_evento {chegada_fila_1 = 0, chegada_fila_2, partida};

// Um evento agendável (nem todos os eventos são agendáveis)
struct Evento {
    Evento *prox_evento; // Ponteiro para o próximo evento a ser tratado
    double momento; // Momento em que o evento deve ser tratado
    Cliente *cliente; // Cliente ao qual o evento se refere
    TipoEvento tipo; // Tipo do evento
};

// Evento sendo tratado atualmente, também representa a fila de eventos
// A fila de eventos é uma lista encadeada, que começa pelo evento_atual
Evento *evento_atual;

// Filas de espera, o cliente não deixa essas filas ao entrar em serviço, apenas
// quando entra na outra fila ou quando parte do sistema
FilaEspera *fila1;
FilaEspera *fila2;

// Ponteiros das rodadas. As rodadas realizadas ficam em uma fila encadeada,
// que começa pela fase_transiente e termina na rodada_atual;
Rodada *fase_transiente;
Rodada *rodada_atual;

// Numero de rodadas já encerradas
unsigned long rodadas_encerradas;

/**
 * Cria uma nova rodada e retorna um ponteiro
*/
Rodada *criar_rodada() {
    Rodada *nova_rodada = malloc(sizeof(Rodada));

    double momento_atual = (evento_atual == NULL)? 0.0 : evento_atual->momento;
    
    nova_rodada->inicio = momento_atual;
    nova_rodada->prox_rodada = NULL;
    nova_rodada->E_W1 = 0.0;
    nova_rodada->E_W2 = 0.0;
    nova_rodada->E_T1 = 0.0;
    nova_rodada->E_T2 = 0.0;
    nova_rodada->E_Nq1 = 0.0;
    nova_rodada->E_Nq2 = 0.0;
    nova_rodada->E_N1 = 0.0;
    nova_rodada->E_N2 = 0.0;
    nova_rodada->ultima_atualizacao_E_Nq1 = momento_atual;
    nova_rodada->ultima_atualizacao_E_N1 = momento_atual;
    nova_rodada->ultima_atualizacao_E_Nq2 = momento_atual;
    nova_rodada->ultima_atualizacao_E_N2 = momento_atual;

    nova_rodada->num_chegadas = 0l;
    nova_rodada->num_partidas = 0l;

    return nova_rodada;
}

/**
 * Inicia a fase transiente
*/
void iniciar_fase_transiente() {
    fase_transiente = criar_rodada();
    rodada_atual = fase_transiente;
}

/**
 * Inicia uma nova rodada (que não é a fase transiente)
*/
void iniciar_nova_rodada() {
    rodada_atual->prox_rodada = criar_rodada();
    rodada_atual = rodada_atual->prox_rodada;
}

/**
 * Cria um novo cliente pertencente à `rodada` e retorna um ponteiro
*/
Cliente *criar_cliente(Rodada *rodada) {
    Cliente *novo_cliente = malloc(sizeof(Cliente));
    novo_cliente->rodada = rodada;
    novo_cliente->prox_cliente = NULL;
    novo_cliente->chegada_estado_atual = 0.0;
    novo_cliente->chegada_fila_atual = 0.0;
    novo_cliente->indice_rodada = rodada->num_chegadas;

    return novo_cliente;
}

/**
 * Cria uma nova fila e retorna um ponteiro
*/
FilaEspera *criar_fila() {
    FilaEspera *nova_fila = malloc(sizeof(FilaEspera));
    nova_fila->num_clientes = 0l;
    nova_fila->primeiro_cliente = NULL;
    nova_fila->ultimo_cliente = NULL;

    return nova_fila;
}

/**
 * Adiciona o `cliente`no final da `fila`
*/
void adicionar_cliente_fila(FilaEspera *fila, Cliente *cliente) {
    if (fila->num_clientes == 0) {
        fila->primeiro_cliente = cliente;
    } else {
        fila->ultimo_cliente->prox_cliente = cliente;
    }

    fila->ultimo_cliente = cliente;
    fila->num_clientes += 1;
}

/**
 * Retorna um ponteiro para o proximo cliente da `fila` e remove ele da `fila`
*/
Cliente *prox_cliente_fila(FilaEspera *fila) {
    Cliente *primeiro_cliente = fila->primeiro_cliente;
    fila->primeiro_cliente = primeiro_cliente->prox_cliente;

    fila->num_clientes -= 1;
    if(fila->num_clientes == 0) {
        fila->ultimo_cliente = NULL;
    }

    return primeiro_cliente;
}

/**
 * Cria um novo evento, onde `momento` é o instante em que ele foi agendado
 * `cliente` é o cliente que ele afeta, e `tipo` é o tipo do evento.
 * Retorna um ponteiro para o evento criado
*/
Evento *criar_evento(double momento,Cliente *cliente, TipoEvento tipo) {
    Evento *novo_evento = malloc(sizeof(Evento));

    novo_evento->prox_evento = NULL;
    novo_evento->momento = momento;
    novo_evento->cliente = cliente;
    novo_evento->tipo = tipo;

    return novo_evento;
}

/**
 * Cria um novo evento, onde `momento` é o instante em que ele foi agendado
 * `cliente` é o cliente que ele afeta, e `tipo` é o tipo do evento, e agenda
 * esse evento na fila de eventos
*/
void agendar_evento(double momento, Cliente *cliente, TipoEvento tipo) {

    // Evento que antecede o evento a ser agendado
    Evento *evento_aterior = evento_atual;

    // Encontra o posição na fila em que o evento agendado deve entrar
    while (evento_aterior->prox_evento != NULL && evento_aterior->prox_evento->momento <= momento) {
        evento_aterior = evento_aterior->prox_evento;
    }

    // Atualiza a fila para encaixar o evento agendado
    Evento *novo_evento = criar_evento(momento, cliente, tipo);
    novo_evento->prox_evento = evento_aterior->prox_evento;
    evento_aterior->prox_evento = novo_evento;
}

/**
 * Retorna uma amostra exponencial com a `taxa` fornecida
*/
double amostra_exponencial(double taxa) {
    double u_0 = (double) rand()/RAND_MAX; // amostra de U(0,1)
    return -(log(u_0)/taxa);
}

/**
 * Retorna a variância dos valores no array X, onde E_X é a média de X, isto é, E[X]
*/
double variancia(double E_X, double *X) {
    double somatorio = 0.0;
    for (unsigned long i = 0ul; i < K; i++) {
        somatorio += (X[i]- E_X)*(X[i]- E_X);
    }
    return somatorio/(K-1);
}

/**
 * Retorna número de pessoas na fila de espera 1 atualmente
*/
long get_Nq1() {
    if (fila1->num_clientes > 0l) {
        return fila1->num_clientes-1;
    }
    return 0l;
}

/**
 * Retorna número de pessoas na fila 1 atualmente (incluindo o que está em serviço)
*/
long get_N1() {
    return fila1->num_clientes;
}

/**
 * Retorna número de pessoas na fila de espera 2 atualmente
*/
long get_Nq2() {
    if (fila1->num_clientes > 0l) {
        return fila2->num_clientes;
    } else if(fila2->num_clientes > 0l) {
        return fila2->num_clientes-1;
    }
    return 0l;
}

/**
 * Retorna número de pessoas na fila 2 atualmente (incluindo o que está em serviço)
*/
long get_N2() {
    return fila2->num_clientes;
}

/**
 * Atualiza o valor de E[Nq1] na rodada atual
*/
void atualizar_E_Nq1() {
    rodada_atual->E_Nq1 += get_Nq1() * (evento_atual->momento - rodada_atual->ultima_atualizacao_E_Nq1);
    rodada_atual->ultima_atualizacao_E_Nq1 = evento_atual->momento;
}

/**
 * Atualiza o valor de E[N1] na rodada atual
*/
void atualizar_E_N1() {
    rodada_atual->E_N1 += get_N1() * (evento_atual->momento - rodada_atual->ultima_atualizacao_E_N1);
    rodada_atual->ultima_atualizacao_E_N1 = evento_atual->momento;
}

/**
 * Atualiza o valor de E[Nq2] na rodada atual
*/
void atualizar_E_Nq2() {
    rodada_atual->E_Nq2 += get_Nq2() * (evento_atual->momento - rodada_atual->ultima_atualizacao_E_Nq2);
    rodada_atual->ultima_atualizacao_E_Nq2 = evento_atual->momento;
}

/**
 * Atualiza o valor de E[N2] na rodada atual
*/
void atualizar_E_N2() {
    rodada_atual->E_N2 += get_N2() * (evento_atual->momento - rodada_atual->ultima_atualizacao_E_N2);
    rodada_atual->ultima_atualizacao_E_N2 = evento_atual->momento;
}

/**
 * Realiza o tratamento do evento atual
*/
void processar_evento_atual() {
    switch (evento_atual->tipo)
    {
    case chegada_fila_1:
        processar_chegada_fila_1(); 
        break;
    case chegada_fila_2:
        processar_chegada_fila_2();
        break;
    case partida:
        processar_partida();
        break;
    
    default:
        break;
    }
}

/**
 * Realiza o tratamento de uma chegada na fila 1
*/
void processar_chegada_fila_1() {

    // Atualiza E[Nq1], E[N1], E[N2], E[Nq2] da rodada atual
    atualizar_E_N1();
    if (get_N1() > 0l) {
        atualizar_E_Nq1();
    }
    if (get_N2() > 0l) {
        atualizar_E_Nq2();
    }

    // Atualiza variáveis auxiliares
    adicionar_cliente_fila(fila1, evento_atual->cliente);
    evento_atual->cliente->chegada_estado_atual = evento_atual->momento;
    evento_atual->cliente->chegada_fila_atual = evento_atual->momento;
    rodada_atual->num_chegadas += 1;

    // Se o numero de coletas da rodada atual foi atingido, inicia uma nova rodada
    if (rodada_atual != fase_transiente && rodada_atual->num_chegadas == K ||
        rodada_atual == fase_transiente && rodada_atual->num_chegadas == K_t) {
            iniciar_nova_rodada();
        }

    // Se não há outros clientes da fila 1 no sistema, o que chegou agora entra em serviço imediatamte
    if(fila1->num_clientes == 1l) {
        processar_chegada_servico_1();
    }

    //Agenda a próxima chegada à fila 1
    double prox_chegada_fila_1 = evento_atual->momento + amostra_exponencial(lambda);
    agendar_evento(prox_chegada_fila_1, criar_cliente(rodada_atual), chegada_fila_1);
}


/**
 * Realiza o tratamento de uma chegada na fila 2
*/
void processar_chegada_fila_2() {

    // Atualiza E[T1] da rodada do cliente
    evento_atual->cliente->rodada->E_T1 += evento_atual->momento - evento_atual->cliente->chegada_fila_atual;
    evento_atual->cliente->chegada_fila_atual = evento_atual->momento;
    evento_atual->cliente->chegada_estado_atual = evento_atual->momento;

    // Atualiza E[Nq1], E[Nq2], E[N1] e E[N2] da rodada atual
    atualizar_E_N1();
    atualizar_E_N2();
    atualizar_E_Nq2();
    if (get_Nq1() > 0l) {
        atualizar_E_Nq1();
    }

    adicionar_cliente_fila(fila2, prox_cliente_fila(fila1));

    // Se houverem clientes na fila 1, um cliente dessa fila entra em serviço,
    // caso contrário, um cliente da fila 2 entra em serviço
    if (fila1->num_clientes > 0l) {
        processar_chegada_servico_1();
    } else {
        processar_chegada_servico_2();
    }
}

/**
 * Realiza o tratamento de uma partida do sistema
*/
void processar_partida() {

    // Atualiza E[T2] da rodada do cliente
    evento_atual->cliente->rodada->E_T2 += evento_atual->momento - evento_atual->cliente->chegada_fila_atual;

    // Se todos os clientes da rodada já partiram, encerra a coleta
    evento_atual->cliente->rodada->num_partidas += 1;
    if (evento_atual->cliente->rodada != fase_transiente && evento_atual->cliente->rodada->num_partidas == K ||
        evento_atual->cliente->rodada == fase_transiente && evento_atual->cliente->rodada->num_partidas == K_t) {
            encerrar_coleta(evento_atual->cliente->rodada);
            rodadas_encerradas += 1ul;
        }

    // Atualiza E[N2] e E[Nq2] da rodada atual
    atualizar_E_N2();
    if (get_Nq2() > 0l) {
        atualizar_E_Nq2();
    }

    free(prox_cliente_fila(fila2));

    // Se tiver outro cliente na fila 2, ele entra em serviço
    // Nunca terá um cliente na fila 1 pois caso contrário a partida teria sido interrompida
    if(fila2->num_clientes > 0l) {
        processar_chegada_servico_2();
    }
}


/**
 * Realiza o tratamento de uma chegada no serviço 1
 * Equivalente à uma partida da fila 1
*/
void processar_chegada_servico_1() {

    // Atualiza E[W1] e o array W1 da rodada do cliente
    fila1->primeiro_cliente->rodada->E_W1 += evento_atual->momento - fila1->primeiro_cliente->chegada_estado_atual;
    if (fila1->primeiro_cliente->rodada != fase_transiente)
        fila1->primeiro_cliente->rodada->W1[fila1->primeiro_cliente->indice_rodada] 
            = evento_atual->momento - fila1->primeiro_cliente->chegada_estado_atual;
    fila1->primeiro_cliente->chegada_estado_atual = evento_atual->momento;

    // interrompe o cliente da fila 2 em serviço (se houver algum)
    interromper_servico_fila_2();

    // Agenda o término do serviço que está começando
    double termino_servico = evento_atual->momento + amostra_exponencial(mu);
    agendar_evento(termino_servico, fila1->primeiro_cliente, chegada_fila_2);
}

/**
 * Realiza o tratamento de uma chegada no serviço 2
 * Equivalente à uma partida da fila 2
*/
void processar_chegada_servico_2() {

    // Atualiza E[W2] e o array W2 da rodada do cliente
    fila2->primeiro_cliente->rodada->E_W2 += evento_atual->momento - fila2->primeiro_cliente->chegada_estado_atual;
    if(fila2->primeiro_cliente->rodada != fase_transiente)
        fila2->primeiro_cliente->rodada->W2[fila2->primeiro_cliente->indice_rodada] 
            += evento_atual->momento - fila2->primeiro_cliente->chegada_estado_atual;
    fila2->primeiro_cliente->chegada_estado_atual = evento_atual->momento;

    // Agenda o término do serviço que está começando
    double termino_servico = evento_atual->momento + amostra_exponencial(mu);
    agendar_evento(termino_servico, fila2->primeiro_cliente, partida);
}


/**
 * Realiza o tratamento de uma interrupção no serviço 2
*/
void interromper_servico_fila_2() {

    // Se a fila 2 está vazia, não há quem interromper
    if (fila2->num_clientes == 0) return;

    // Cancela o evento de partida do sistema, se este estiver agendado
    for(Evento *e = evento_atual; e->prox_evento != NULL; e = e->prox_evento) {
        if (e->prox_evento->tipo == partida) {

            Evento *partida = e->prox_evento;
            e->prox_evento = partida->prox_evento;

            partida->cliente->chegada_estado_atual = evento_atual->momento;

            free(partida);
            return;
        }
    }
}

/**
 * Encerra a coleta da rodada
*/
void encerrar_coleta(Rodada *rodada) {
    unsigned long num_coletas = rodada->num_chegadas;
    double duracao_rodada = rodada->prox_rodada->inicio - rodada->inicio;

    // Atualiza pela última vez o número de pessoas nas filas
    atualizar_E_Nq1();
    atualizar_E_Nq2();
    atualizar_E_N1();
    atualizar_E_N2();

    // Normaliza as métricas coletadas (que antes eram apenas somátórios das coletas)
    rodada->E_W1 /= num_coletas;
    rodada->E_W2 /= num_coletas;
    rodada->E_T1 /= num_coletas;
    rodada->E_T2 /= num_coletas;
    rodada->E_Nq1 /= duracao_rodada;
    rodada->E_Nq2 /= duracao_rodada;
    rodada->E_N1 /= duracao_rodada;
    rodada->E_N2 /= duracao_rodada;

    // Calcula as variâncias
    rodada->V_W1 = variancia(rodada->E_W1, &rodada->W1[0]);
    rodada->V_W2 = variancia(rodada->E_W2, &rodada->W2[0]);
    if (PRINT_RESULTADO_RODADA) {
        printf("E[W1]: %f\n", rodada->E_W1);
        printf("E[T1]: %f\n", rodada->E_T1);
        printf("E[Nq1]: %f\n", rodada->E_Nq1);
        printf("E[N1]: %f\n", rodada->E_N1);
        printf("E[W2]: %f\n", rodada->E_W2);
        printf("E[T2]: %f\n", rodada->E_T2);
        printf("E[Nq2]: %f\n", rodada->E_Nq2);
        printf("E[N2]: %f\n", rodada->E_N2);
        printf("V[W1]: %f\n", rodada->V_W1);
        printf("V[W2]: %f\n", rodada->V_W2);
        printf("\n\n");
    }

}

#define Z 1.959963 //Número da tabela Z

/**
 * Gera um intervalo de confianca para uma média coletada.
 * IC = media +- Z*variancia/sqrt(n)
 * onde
 * `n` é o número de experimentos e `Z` é um valor retirado da tabela Z.
 * `intervalo_confianca` é o array de duas posições que guarda
 * o limite inferior e superior do intervalo de confianca. A posição 0
 * é o limite inferior, e a posição 1 é o limite superior.
 * 
 * A função nao retorna nada diretamente, apenas altera os valores dentro do próprio
 * ponteiro passado como argumento em `intervalo_confianca`
*/
void gerar_intervalo_media(double media, double variancia, int n, double *intervalo_confianca) {
    double limite_inferior = media;
    double limite_superior = media;
    double valor_auxiliar = (Z * variancia) / sqrt(n);
    limite_inferior -= valor_auxiliar;
    limite_superior += valor_auxiliar;
    intervalo_confianca[0] = limite_inferior;
    intervalo_confianca[1] = limite_superior;
}


/**
 * Gera um intervalo de confianca para uma variância coletada.
 * `variancia` é a variância coletada
 * `precisao` é a precisão dd IC
 * `intervalo_confianca` é o array de duas posições que guarda
 * o limite inferior e superior do intervalo de confianca. A posição 0
 * é o limite inferior, e a posição 1 é o limite superior.
 * 
 * A função nao retorna nada diretamente, apenas altera os valores dentro do próprio
 * ponteiro passado como argumento em `intervalo_confianca`
*/
void gerar_intervalo_variancia(double variancia, double precisao, double *intervalo_confianca) {
    intervalo_confianca[0] = variancia * (1-precisao);
    intervalo_confianca[1] = variancia * (1+precisao);
}

/**
 * Retorna a precisão do `intervalo_confianca`
*/
double precisao_IC(double *intervalo_confianca) {
    return (intervalo_confianca[1] - intervalo_confianca[0])/(intervalo_confianca[1] + intervalo_confianca[0]);
}

/**
 * Calcula os ICs da simulação e imprime o resultado na tela
*/
void calcular_IC_rodadas() {

    // Calcula a média das métrica coletadas

    double media_E_W1 = 0.0;
    double media_E_T1 = 0.0;
    double media_E_Nq1 = 0.0;
    double media_E_N1 = 0.0;
    double media_E_W2 = 0.0;
    double media_E_T2 = 0.0;
    double media_E_Nq2 = 0.0;
    double media_E_N2 = 0.0;
    double media_V_W1 = 0.0;
    double media_V_W2 = 0.0;

    Rodada * rodada = fase_transiente;
    for (unsigned long i = 0ul; i < NUM_RODADAS; i++) {
        rodada = rodada->prox_rodada;

        media_E_W1 += rodada->E_W1;
        media_E_T1 += rodada->E_T1;
        media_E_Nq1 += rodada->E_Nq1;
        media_E_N1 += rodada->E_N1;
        media_E_W2 += rodada->E_W2;
        media_E_T2 += rodada->E_T2;
        media_E_Nq2 += rodada->E_Nq2;
        media_E_N2 += rodada->E_N2;
        media_V_W1 += rodada->V_W1;
        media_V_W2 += rodada->V_W2;
    }
    media_E_W1 /= NUM_RODADAS;
    media_E_T1 /= NUM_RODADAS;
    media_E_Nq1 /= NUM_RODADAS;
    media_E_N1 /= NUM_RODADAS;
    media_E_W2 /= NUM_RODADAS;
    media_E_T2 /= NUM_RODADAS;
    media_E_Nq2 /= NUM_RODADAS;
    media_E_N2 /= NUM_RODADAS;
    media_V_W1 /= NUM_RODADAS;
    media_V_W2 /= NUM_RODADAS;

    // Cacula a variância das médias coletadas

    double var_E_W1 = 0.0;
    double var_E_T1 = 0.0;
    double var_E_Nq1 = 0.0;
    double var_E_N1 = 0.0;
    double var_E_W2 = 0.0;
    double var_E_T2 = 0.0;
    double var_E_Nq2 = 0.0;
    double var_E_N2 = 0.0;
    rodada = fase_transiente;
    for (unsigned long i = 0ul; i < NUM_RODADAS; i++) {
        rodada = rodada->prox_rodada;

        var_E_W1 += (rodada->E_W1 - media_E_W1)*(rodada->E_W1 - media_E_W1);
        var_E_T1 += (rodada->E_T1 - media_E_T1)*(rodada->E_T1 - media_E_T1);
        var_E_Nq1 += (rodada->E_Nq1 - media_E_Nq1)*(rodada->E_Nq1 - media_E_Nq1);
        var_E_N1 += (rodada->E_N1 - media_E_N1)*(rodada->E_N1 - media_E_N1);
        var_E_W2 += (rodada->E_W2 - media_E_W2)*(rodada->E_W2 - media_E_W2);
        var_E_T2 += (rodada->E_T2 - media_E_T2)*(rodada->E_T2 - media_E_T2);
        var_E_Nq2 += (rodada->E_Nq2 - media_E_Nq2)*(rodada->E_Nq2 - media_E_Nq2);
        var_E_N2 += (rodada->E_N2 - media_E_N2)*(rodada->E_N2 - media_E_N2);
    }

    var_E_W1 /= (NUM_RODADAS-1);
    var_E_T1 /= (NUM_RODADAS-1);
    var_E_Nq1 /= (NUM_RODADAS-1);
    var_E_N1 /= (NUM_RODADAS-1);
    var_E_W2 /= (NUM_RODADAS-1);
    var_E_T2 /= (NUM_RODADAS-1);
    var_E_Nq2 /= (NUM_RODADAS-1);
    var_E_N2 /= (NUM_RODADAS-1);

    // Gera os ICs e imprime na tela
    double *IC = malloc(sizeof(double)*2);
    gerar_intervalo_media(media_E_W1, var_E_W1, NUM_RODADAS, IC);
    printf("E[W1]: %f - %f - %f (p = %.2f%%)\n", IC[0], media_E_W1, IC[1], precisao_IC(IC)*100);
    gerar_intervalo_media(media_E_T1, var_E_T1, NUM_RODADAS, IC);
    printf("E[T1]: %f - %f - %f (p = %.2f%%)\n", IC[0], media_E_T1, IC[1], precisao_IC(IC)*100);
    gerar_intervalo_media(media_E_Nq1, var_E_Nq1, NUM_RODADAS, IC);
    printf("E[Nq1]: %f - %f - %f (p = %.2f%%)\n", IC[0], media_E_Nq1, IC[1], precisao_IC(IC)*100);
    gerar_intervalo_media(media_E_N1, var_E_N1, NUM_RODADAS, IC);
    printf("E[N1]: %f - %f - %f (p = %.2f%%)\n", IC[0], media_E_N1, IC[1], precisao_IC(IC)*100);
    gerar_intervalo_media(media_E_W2, var_E_W2, NUM_RODADAS, IC);
    printf("E[W2]: %f - %f - %f (p = %.2f%%)\n", IC[0], media_E_W2, IC[1], precisao_IC(IC)*100);
    gerar_intervalo_media(media_E_T2, var_E_T2, NUM_RODADAS, IC);
    printf("E[T2]: %f - %f - %f (p = %.2f%%)\n", IC[0], media_E_T2, IC[1], precisao_IC(IC)*100);
    gerar_intervalo_media(media_E_Nq2, var_E_Nq2, NUM_RODADAS, IC);
    printf("E[Nq2]: %f - %f - %f (p = %.2f%%)\n", IC[0], media_E_Nq2, IC[1], precisao_IC(IC)*100);
    gerar_intervalo_media(media_E_N2, var_E_N2, NUM_RODADAS, IC);
    printf("E[N2]: %f - %f - %f (p = %.2f%%)\n", IC[0], media_E_N2, IC[1], precisao_IC(IC)*100);
    gerar_intervalo_variancia(media_V_W1, p_variancia, IC);
    printf("V[W1]: %f - %f - %f (p = %.2f%%)\n", IC[0], media_V_W1, IC[1], precisao_IC(IC)*100);
    gerar_intervalo_variancia(media_V_W2, p_variancia, IC);
    printf("V[W2]: %f - %f - %f (p = %.2f%%)\n", IC[0], media_V_W2, IC[1], precisao_IC(IC)*100);
    printf("\n\n");
}

int main(int argc, char const *argv[])
{
    // marca o incio da simulação
    time_t inicio = time(NULL);
    
    // Inicia as variáveis globais
    fila1 = criar_fila();
    fila2 = criar_fila();
    rodadas_encerradas = 0ul;
    iniciar_fase_transiente();
    srand(SEED);

    // Agenda a primeira chegada
    evento_atual = criar_evento(amostra_exponencial(lambda), criar_cliente(fase_transiente), chegada_fila_1);

    // Realiza a simulação propriamente dita, agenda e processa os eventos
    while(rodadas_encerradas < NUM_RODADAS+1) {
        processar_evento_atual();

        Evento *temp = evento_atual;
        evento_atual = evento_atual->prox_evento;
        free(temp);
    }

    // Imprime na tela os ICs coletados pela simulação.
    // Os resultados são impressos no seguinte formato:
    // [Métrica coletada]: [Limite inferior] - [média do IC] - [Limite superior]
    calcular_IC_rodadas();

    // marca o final da simulação
    time_t fim = time(NULL);

    // imprime o tempo da simulação na tela
    printf("A simulação levou %ld segundos.\n", fim-inicio);

    return 0;
}