use std::env;
use std::net::{Ipv4Addr, SocketAddrV4};
use std::time::Duration;
use tokio::net::TcpListener;
use tokio::prelude::*;
use tokio::runtime::Builder;
use tokio::time::timeout;

static RESPONSE: &[u8] = b"HTTP/1.1 200 OK\nContent-Length: 12\n\nHello world!";

fn main() {
    let ip = env::args().nth(1).unwrap().parse::<Ipv4Addr>().unwrap();
    let port = env::args().nth(2).unwrap().parse::<u16>().unwrap();
    let addr = SocketAddrV4::new(ip, port);

    let num_threads = env::args().nth(3).unwrap().parse::<usize>().unwrap();

    Builder::new()
        .threaded_scheduler()
        .enable_all()
        .core_threads(num_threads)
        .build()
        .unwrap()
        .block_on(async {
            let mut listener = TcpListener::bind(&addr).await.unwrap();
            loop {
                let (mut stream, _) = listener.accept().await.unwrap();
                tokio::spawn(async move {
                    let mut buf = [0u8; 1024];
                    loop {
                        let read_future = stream.read(&mut buf);
                        let read_timeout = timeout(Duration::from_secs(5), read_future);
                        let num_read = match read_timeout.await {
                            Err(e) => {
                                eprintln!("Read timeout: {:?}", e);
                                return;
                            }
                            Ok(t) => match t {
                                Err(e) => {
                                    eprintln!("Reading failed: {:?}", e);
                                    return;
                                }
                                Ok(n) => n,
                            },
                        };
                        if num_read == 0 {
                            return;
                        }

                        let write_future = stream.write(RESPONSE);
                        let write_timeout = timeout(Duration::from_secs(5), write_future);
                        match write_timeout.await {
                            Err(e) => {
                                eprintln!("Write timeout: {:?}", e);
                                return;
                            }
                            Ok(t) => match t {
                                Err(e) => {
                                    eprintln!("Writing failed: {:?}", e);
                                    return;
                                }
                                Ok(n) => {
                                    if n != RESPONSE.len() {
                                        panic!("Writing failed")
                                    }
                                }
                            },
                        };
                    }
                });
            }
        });
}
