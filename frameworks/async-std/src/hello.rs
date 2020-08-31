use async_std::net::TcpListener;
use async_std::prelude::*;
use async_std::task;
use std::env;
use std::net::{Ipv4Addr, SocketAddrV4};
use std::panic;

static RESPONSE: &[u8] = b"HTTP/1.1 200 OK\nContent-Length: 12\n\nHello world!";

fn main() {
    let ip = env::args().nth(1).unwrap().parse::<Ipv4Addr>().unwrap();
    let port = env::args().nth(2).unwrap().parse::<u16>().unwrap();
    let addr = SocketAddrV4::new(ip, port);

    let num_threads = env::args().nth(3).unwrap().parse::<u32>().unwrap();
    env::set_var("ASYNC_STD_THREAD_COUNT", num_threads.to_string());

    task::block_on(async {
        let listener = TcpListener::bind(&addr).await.unwrap();
        loop {
            let (mut stream, _) = listener.accept().await.unwrap();
            task::spawn(async move {
                let mut buf = [0u8; 1024];
                loop {
                    let read_future = stream.read(&mut buf);
                    let num_read = match read_future.await {
                        Err(e) => {
                            eprintln!("Reading failed: {:?}", e);
                            return;
                        }
                        Ok(n) => n,
                    };
                    if num_read == 0 {
                        return;
                    }

                    let write_future = stream.write(RESPONSE);
                    match write_future.await {
                        Err(e) => {
                            eprintln!("Writing failed: {:?}", e);
                            return;
                        }
                        Ok(n) => {
                            if n != RESPONSE.len() {
                                panic!("Writing failed")
                            }
                        }
                    }
                }
            });
        }
    });
}
