use std::io;

fn main() {
    let mut input = String::new();

    // Matrix A: Enter size
    println!("Enter A rows:");
    io::stdin().read_line(&mut input).expect("Input error");
    let rows: usize = input.trim().parse().expect("Invalid rows");

    input.clear();

    println!("Enter A cols:");
    io::stdin().read_line(&mut input).expect("Input error");
    let cols: usize = input.trim().parse().expect("Invalid cols");

    // Matrix A: Create and enter values
    let mut matrix = vec![vec![0; cols]; rows];

    for i in 0..rows {
        for j in 0..cols {
            input.clear();

            println!("Enter A[{}][{}]:", i, j);
            io::stdin().read_line(&mut input).expect("Input error");

            let value: i32 = input.trim().parse().expect("Invalid number");
            matrix[i][j] = value;
        }
    }

    println!("Matrix A: {:?}", matrix);

    // Matrix B: Enter size
    input.clear();

    println!("Enter B rows:");
    io::stdin().read_line(&mut input).expect("Input error");
    let b_rows: usize = input.trim().parse().expect("Invalid rows");

    input.clear();

    println!("Enter B cols:");
    io::stdin().read_line(&mut input).expect("Input error");
    let b_cols: usize = input.trim().parse().expect("Invalid cols");

    // Check matrix sizes
    if rows != b_rows || cols != b_cols {
        println!("Error: Matrix sizes must match.");
        return;
    }

    // Matrix B: Create and enter values
    let mut matrix_b = vec![vec![0; b_cols]; b_rows];

    for i in 0..b_rows {
        for j in 0..b_cols {
            input.clear();

            println!("Enter B[{}][{}]:", i, j);
            io::stdin().read_line(&mut input).expect("Input error");

            let value: i32 = input.trim().parse().expect("Invalid number");
            matrix_b[i][j] = value;
        }
    }

    println!("Matrix B: {:?}", matrix_b);

    // Matrix addition
    let mut result = vec![vec![0; cols]; rows];

    for i in 0..rows {
        for j in 0..cols {
            result[i][j] = matrix[i][j] + matrix_b[i][j];
        }
    }

    println!("A + B: {:?}", result);
}
