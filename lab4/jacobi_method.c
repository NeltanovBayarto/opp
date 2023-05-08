#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <mpi.h>

#define N 320
#define a 10e5
#define epsilon 10e-8
#define idx(i, j, k) (N * N * i + N * j + k)

#define D_X 2
#define D_Y 2
#define D_Z 2

#define X_0 (-1)
#define Y_0 (-1)
#define Z_0 (-1)

int size = 0;
int rank = 0;

const double H_X = D_X / (double) (N - 1);
const double H_Y = D_Y / (double) (N - 1);
const double H_Z = D_Z / (double) (N - 1);

double H_X2 = H_X * H_X;
double H_Y2 = H_Y * H_Y;
double H_Z2 = H_Z * H_Z;

double phi(double x, double y, double z) {
    return x * x + y * y + z * z;
}

double rho(double x, double y, double z) {
    return 6 - a * phi(x, y, z);
}

double X(int i) {
    return X_0 + i * H_X;
}

double Y(int j) {
    return Y_0 + j * H_Y;
}

double Z(int k) {
    return Z_0 + k * H_Z;
}

void init_phi(int layer_height, double *current_layer) {
    for (int i = 0; i < layer_height + 2; i++) {
        int relative_Z = i + ((rank * layer_height) - 1);
        double z = Z(relative_Z);

        for (int j = 0; j < N; j++) {
            double x = X(j);

            for (int k = 0; k < N; k++) {
                double y = Y(k);

                if (k != 0 && k != N - 1 &&
                    j != 0 && j != N - 1 &&
                    z != Z_0 && z != Z_0 + D_Z) {
                    current_layer[idx(i, j, k)] = 0;
                } else {
                    current_layer[idx(i, j, k)] = phi(x, y, z);
                }

            }
        }
    }
}

void print_cube(double *A) {
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            for (int k = 0; k < N; k++) {
                printf(" %7.4f", A[idx(i, j, k)]);
            }
            printf(";");
        }
        printf("\n");
    }
}

double calc_delta(double *omega) {
    double deltaMax = DBL_MIN;
    double x, y, z;
    for (int i = 0; i < N; i++) {
        x = X(i);
        for (int j = 0; j < N; j++) {
            y = Y(j);
            for (int k = 0; k < N; k++) {
                z = Z(k);
                deltaMax = fmax(deltaMax, fabs(omega[idx(i, j, k)] - phi(x, y, z)));
            }
        }
    }

    return deltaMax;
}

double update_layer(int relative_Z_coordinate, int layer_idx, double *current_layer, double *current_layer_buf) {
    int absolute_Z_coordinate = relative_Z_coordinate + layer_idx; // во всей омега
    double delta_max = DBL_MIN;
    double x, y, z;

    if (absolute_Z_coordinate == 0 || absolute_Z_coordinate == N - 1) {
        memcpy(current_layer_buf + layer_idx * N * N, current_layer + layer_idx * N * N, N * N * sizeof(double));
        delta_max = 0;
    } else {
        z = Z(absolute_Z_coordinate);

        for (int i = 0; i < N; i++) {
            x = X(i);
            for (int j = 0; j < N; j++) {
                y = Y(j);

                if (i == 0 || i == N - 1 || j == 0 || j == N - 1) {
                    current_layer_buf[idx(layer_idx, i, j)] = current_layer[idx(layer_idx, i, j)];
                } else {
                    current_layer_buf[idx(layer_idx, i, j)] =
                            ((current_layer[idx(layer_idx + 1, i, j)] + current_layer[idx(layer_idx - 1, i, j)]) / H_Z2 +
                             (current_layer[idx(layer_idx, i + 1, j)] + current_layer[idx(layer_idx, i - 1, j)]) / H_X2 +
                             (current_layer[idx(layer_idx, i, j + 1)] + current_layer[idx(layer_idx, i, j - 1)]) / H_Y2 -
                             rho(x, y, z)) / (2 / H_X2 + 2 / H_Y2 + 2 / H_Z2 + a);

                    if (fabs(current_layer_buf[idx(layer_idx, i, j)] - current_layer[idx(layer_idx, i, j)]) > delta_max) {
                        delta_max = current_layer_buf[idx(layer_idx, i, j)] - current_layer[idx(layer_idx, i, j)];
                    }

                }
            }
        }
    }

    return delta_max;
}

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Request req[4];

    if (N % size && rank == 0) {
        printf("Grid size %d should be a multiple of the ProcNum\n", N);
        return 0;
    }

    double *omega;

    double global_max_delta = DBL_MAX;

    int layer_size = N / size;
    int layer_Z_coordinate = rank * layer_size - 1;

    int extended_layer_size = (layer_size + 2) * N * N;
    double *current_layer = malloc(extended_layer_size * sizeof(double));
    double *current_layer_buf = malloc(extended_layer_size * sizeof(double));

    init_phi(layer_size, current_layer);

    double start_time_s = MPI_Wtime();
    do {
        double proc_max_delta = DBL_MIN;
        double tmp_max_delta;

        if (rank != 0) {
            MPI_Isend(current_layer_buf + N * N, N * N, MPI_DOUBLE,
                      rank - 1, 888, MPI_COMM_WORLD, &req[1]);

            MPI_Irecv(current_layer_buf, N * N, MPI_DOUBLE,
                      rank - 1, 888, MPI_COMM_WORLD, &req[0]);
        }

        if (rank != size - 1) {
            MPI_Isend(current_layer_buf + N * N * layer_size, N * N, MPI_DOUBLE,
                      rank + 1, 888, MPI_COMM_WORLD, &req[3]);

            MPI_Irecv(current_layer_buf + N * N * (layer_size + 1), N * N, MPI_DOUBLE,
                      rank + 1, 888, MPI_COMM_WORLD, &req[2]);
        }

        for (int layer_idx = 2; layer_idx < layer_size; layer_idx++) {
            tmp_max_delta = update_layer(layer_Z_coordinate, layer_idx, current_layer, current_layer_buf);
            proc_max_delta = fmax(proc_max_delta, tmp_max_delta);
        }

        if (rank != size - 1) {
            MPI_Wait(&req[2], MPI_STATUS_IGNORE);
            MPI_Wait(&req[3], MPI_STATUS_IGNORE);
        }

        if (rank != 0) {
            MPI_Wait(&req[0], MPI_STATUS_IGNORE);
            MPI_Wait(&req[1], MPI_STATUS_IGNORE);
        }

        tmp_max_delta = update_layer(layer_Z_coordinate, 1, current_layer, current_layer_buf);
        proc_max_delta = fmax(proc_max_delta, tmp_max_delta);

        tmp_max_delta = update_layer(layer_Z_coordinate, layer_size, current_layer, current_layer_buf);
        proc_max_delta = fmax(proc_max_delta, tmp_max_delta);

        memcpy(current_layer, current_layer_buf, extended_layer_size * sizeof(double));

        MPI_Allreduce(&proc_max_delta, &global_max_delta, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);

    } while (global_max_delta > epsilon);

    free(current_layer_buf);

    double end_time_s = MPI_Wtime();

    if (rank == 0) {
        omega = malloc(N * N * N * sizeof(double));
    }

    MPI_Gather(current_layer + N * N, layer_size * N * N, MPI_DOUBLE, omega,
               layer_size * N * N, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        printf("Time taken: %lf s\n", end_time_s - start_time_s);
        printf("Delta: %lf", calc_delta(omega));
        free(omega);
    }

    free(current_layer);

    MPI_Finalize();
    return 0;
}
