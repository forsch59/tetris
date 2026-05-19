import asyncio
import random

LISTEN_PORT = 12346
SERVER_PORT = 12345
SERVER_HOST = '127.0.0.1'

# Network simulation parameters
BASE_LATENCY = 0.15  # 150ms base delay
JITTER = 0.05        # +/- 50ms random jitter
FRAGMENTATION_PROBABILITY = 0.2 # 20% chance to fragment a packet (tests your read logic)

async def delay_data():
    """Simulate network delay and jitter"""
    delay = BASE_LATENCY + random.uniform(-JITTER, JITTER)
    if delay > 0:
        await asyncio.sleep(delay)

async def proxy_stream(reader: asyncio.StreamReader, writer: asyncio.StreamWriter, direction: str):
    try:
        while True:
            data = await reader.read(4096)
            if not data:
                break
            
            # Simulate latency
            await delay_data()
            
            # Simulate fragmentation (split data into smaller chunks)
            # This is crucial for testing because real TCP streams often arrive in chunks
            # and your client/server must buffer correctly.
            if random.random() < FRAGMENTATION_PROBABILITY and len(data) > 1:
                split_point = random.randint(1, len(data) - 1)
                writer.write(data[:split_point])
                await writer.drain()
                
                # Small delay between chunks
                await asyncio.sleep(random.uniform(0.01, 0.05))
                
                writer.write(data[split_point:])
            else:
                writer.write(data)
            
            await writer.drain()
    except asyncio.CancelledError:
        pass
    except Exception as e:
        print(f"[{direction}] Stream error: {e}")
    finally:
        writer.close()

async def handle_client(client_reader, client_writer):
    client_addr = client_writer.get_extra_info('peername')
    print(f"[*] New connection from {client_addr}, routing through lag proxy...")
    
    try:
        server_reader, server_writer = await asyncio.open_connection(SERVER_HOST, SERVER_PORT)
    except Exception as e:
        print(f"[!] Failed to connect to actual server at {SERVER_PORT}: {e}")
        client_writer.close()
        return

    # Run bidirectional proxying concurrently
    task1 = asyncio.create_task(proxy_stream(client_reader, server_writer, "C->S"))
    task2 = asyncio.create_task(proxy_stream(server_reader, client_writer, "S->C"))
    
    done, pending = await asyncio.wait([task1, task2], return_when=asyncio.FIRST_COMPLETED)
    
    print(f"[*] Connection {client_addr} closed.")
    for p in pending:
        p.cancel()
    server_writer.close()
    client_writer.close()

async def main():
    server = await asyncio.start_server(handle_client, '127.0.0.1', LISTEN_PORT)
    print(f"=== TCP Lag Proxy Started ===")
    print(f"Listening on port: {LISTEN_PORT}")
    print(f"Forwarding to port: {SERVER_PORT}")
    print(f"Base Latency: {BASE_LATENCY * 1000:.0f}ms")
    print(f"Jitter: +/- {JITTER * 1000:.0f}ms")
    print(f"Fragmentation Chance: {FRAGMENTATION_PROBABILITY * 100:.0f}%")
    print("=============================")
    async with server:
        await server.serve_forever()

if __name__ == '__main__':
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nProxy stopped.")
