#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <mpi.h>

#define RANK_ROOT 0

void fill_matrix(double *matrix, int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            matrix[i * cols + j] = j;
        }
    }
}

void print_matrix(double *matrix, int rows, int cols) {
    for (int i = 0; i < rows * cols; i++) {
        printf("%lf ", matrix[i]);

        if ((i + 1) % cols == 0) {
            printf("\n");
        }
    }
}

int run(void) {
    int dims[2] = {0, 0}, periods[2] = {0, 0}, coords[2], reorder = 1;
    int size, rank, sizey, sizex, ranky, rankx;

    MPI_Comm_size(MPI_COMM_WORLD, &size);

    MPI_Dims_create(size, 2, dims);
    sizex = dims[0];
    sizey = dims[1];

    MPI_Comm comm2d;
    MPI_Cart_create(MPI_COMM_WORLD, 2, dims, periods, reorder, &comm2d); // создание 2d решетки
    MPI_Comm_rank(comm2d, &rank); // получение номеров процесса в 2d решетке (старая нумерация)
    MPI_Cart_get(comm2d, 2, dims, periods, coords); // получение номера процесса в виде координат решетки
    rankx = coords[0];
    ranky = coords[1];

    if (RANK_ROOT == rank) {
        printf("Size of 2d cart: %dx%d\n", sizex, sizey);
    }

    MPI_Comm commOrdinate, commAbscissa;
    MPI_Comm_split(comm2d, ranky, rankx, &commAbscissa);
    MPI_Comm_split(comm2d, rankx, ranky, &commOrdinate);

    int n1 = 11, n2 = 12, n3 = 16;

    double *A = NULL;
    double *B = NULL;
    double *C = NULL;

    double *part_A = calloc((n1 / sizey + 1) * n2, sizeof(double));
    double *part_B = calloc(n2 * (n3 / sizex), sizeof(double));
    double *part_C = calloc((n1 / sizey + 1) * (n3 / sizex), sizeof(double));

    if (RANK_ROOT == rank) {
        A = calloc(n1 * n2, sizeof(double));
        B = calloc(n2 * n3, sizeof(double));
        C = calloc(n1 * n3, sizeof(double));

        fill_matrix(A, n1, n2);
        fill_matrix(B, n2, n3);
        print_matrix(A, n1, n2);
        printf("\n");
    }
    int *sendcounts = malloc(sizey * sizeof(int));
    int *displs = malloc(sizey * sizeof(int));

    // Подготовка sencounts и displs к подаче в scatterv
    int nmin = n1 / sizey;
    int nextra = n1 % sizey;
    int k = 0;
    for (int i = 0; i < sizey; i++) {
        if (i < nextra) {
            sendcounts[i] = (nmin + 1) * n2;
        } else {
            sendcounts[i] = nmin * n2;
        }
        displs[i] = k;
        k += sendcounts[i];
    }

    if (RANK_ROOT == rankx) {
        MPI_Scatterv(A, sendcounts, displs, MPI_DOUBLE,
                     part_A, (n1 / sizey + 1) * n2, MPI_DOUBLE, RANK_ROOT, commOrdinate);
    }

    MPI_Bcast(part_A, (n1 / sizey + 1) * n2, MPI_DOUBLE, RANK_ROOT, commAbscissa);

    sleep(rank);
    printf("RANK: (%d, %d)\n", rankx, ranky);
    print_matrix(part_A, n1 / sizey + 1, n2);
    printf("\n");

    const int columns_per_process = n3 / sizex;

    MPI_Datatype vertical_double_slice;
    MPI_Datatype vertical_double_slice_resized;

    MPI_Type_vector(
            /* blocks count - number of rows */ n2,
            /* block length  */ columns_per_process,
            /* stride - block start offset */ n3,
            /* old type - element type */ MPI_DOUBLE,
            /* new type */ &vertical_double_slice
    );
    MPI_Type_commit(&vertical_double_slice);

    MPI_Type_create_resized(
            vertical_double_slice,
            /* lower bound */ 0,
            /* extent - size in bytes */ (int) (columns_per_process * sizeof(double)),
            /* new type */ &vertical_double_slice_resized
    );

    MPI_Type_commit(&vertical_double_slice_resized);

    if (RANK_ROOT == ranky) {
        MPI_Scatter(
                /* send buffer */ B,
                /* number of <send data type> elements sent */ 1,
                /* send data type */ vertical_double_slice_resized,
                /* recv buffer */ part_B,
                /* number of <recv data type> elements received */ n2 * columns_per_process,
                /* recv data type */ MPI_DOUBLE,
                                  RANK_ROOT,
                                  commAbscissa
        );
    }
    MPI_Bcast(part_B, n2 * columns_per_process, MPI_DOUBLE, RANK_ROOT, commOrdinate);

    sleep(size + rank);
    printf("RANK: (%d, %d)\n", rankx, ranky);
    print_matrix(part_B, n2, columns_per_process);
    printf("\n");

    MPI_Type_free(&vertical_double_slice_resized);
    MPI_Type_free(&vertical_double_slice);

    if (RANK_ROOT == rank) {
        free(A);
        free(B);
        free(C);
    }
    free(part_A);
    free(part_B);
    free(part_C);

    return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    const int exit_code = run();

    MPI_Finalize();

    return exit_code;
}
