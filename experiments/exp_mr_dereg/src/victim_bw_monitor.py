#!/usr/bin/env python3
"""
Victim带宽监测器

作为Victim运行RDMA带宽测试，同时实时记录带宽数据。
可以配合Attacker使用，观测攻击对Victim带宽的影响。

用法:
    # 作为服务器（对端）
    python3 victim_bw_monitor.py --mode=server --device=mlx5_0

    # 作为客户端（本机）
    python3 victim_bw_monitor.py --mode=client --server=192.168.106.2 \
        --duration=30 --output=victim.csv
"""

import subprocess
import time
import csv
import argparse
import sys
import os
import signal

# 全局变量用于信号处理
running = True


def signal_handler(sig, frame):
    global running
    print("\n[INFO] Received interrupt signal, shutting down...")
    running = False


def parse_bw_output(line):
    """从ib_write_bw输出解析带宽"""
    # ib_write_bw 输出示例:
    # #bytes     #iterations    BW peak[MB/sec]    BW average[MB/sec]   MsgRate[Mpps]
    # 65536      1000           8824.9             8824.6               0.147076
    parts = line.strip().split()
    if len(parts) >= 4 and parts[0].isdigit():
        try:
            bw_mbps = float(parts[3])  # BW average in MB/sec
            bw_gbps = bw_mbps * 8 / 1000  # Convert to Gbps
            return bw_gbps
        except (ValueError, IndexError):
            pass
    return None


def run_server(args):
    """运行ib_send_bw服务器 (等待连接，不指定IP)"""
    cmd = [
        'ib_send_bw',
        '-F', '-R',
        '-d', args.device,
        '--report_gbits',
        '--run_infinitely'
    ]
    
    print(f"[Server] Starting ib_write_bw server on {args.device}:{args.port}")
    print(f"[Server] Command: {' '.join(cmd)}")
    
    try:
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        
        # 等待客户端连接
        print("[Server] Waiting for client connection...")
        for line in proc.stdout:
            print(f"[Server] {line.strip()}")
            if "received" in line.lower() or "bytes" in line.lower():
                break
        
        # 保持运行直到被中断
        while running:
            time.sleep(1)
            
        proc.terminate()
        proc.wait()
        
    except FileNotFoundError:
        print("[ERROR] ib_send_bw not found. Please install perftest package.")
        print("        sudo apt-get install perftest")
        return 1
    
    return 0


def run_client(args):
    """运行ib_send_bw客户端并记录带宽 (连接指定IP)"""
    
    cmd = [
        'ib_send_bw',
        '-F', '-R',
        '-d', args.device,
        args.server,  # 指定server IP
        '--report_gbits',
        '-D', str(args.duration),  # 持续测试模式
        '-t', '1',  # 每秒报告一次
        '-q', '4',  # 4个QP
        '-s', str(args.size),  # 消息大小
    ]
    
    print(f"[Client] Connecting to server {args.server}:{args.port}")
    print(f"[Client] Duration: {args.duration} seconds")
    print(f"[Client] Output: {args.output}")
    print(f"[Client] Command: {' '.join(cmd)}")
    print()
    
    # 准备CSV输出
    samples = []
    start_time = time.time()
    
    try:
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            universal_newlines=True
        )
        
        print("[Client] Running bandwidth test...")
        print("-" * 60)
        
        for line in proc.stdout:
            if not running:
                proc.terminate()
                break
                
            line = line.strip()
            if not line:
                continue
                
            # 解析带宽数据
            bw = parse_bw_output(line)
            if bw is not None:
                timestamp = time.time() - start_time
                phase = 1 if timestamp >= 5.0 else 0  # 5秒后标记为攻击阶段
                
                samples.append({
                    'timestamp': round(timestamp, 3),
                    'bandwidth_gbps': round(bw, 2),
                    'phase': phase
                })
                
                # 实时显示
                phase_str = "[BASELINE]" if phase == 0 else "[ATTACK]  "
                print(f"{phase_str} Time: {timestamp:6.2f}s | BW: {bw:6.2f} Gbps")
            else:
                # 打印其他输出
                print(f"[INFO] {line}")
        
        proc.wait()
        
    except FileNotFoundError:
        print("[ERROR] ib_write_bw not found. Please install perftest package.")
        print("        sudo apt-get install perftest")
        return 1
    except KeyboardInterrupt:
        print("\n[Client] Interrupted by user")
        proc.terminate()
    
    # 保存结果
    if samples:
        with open(args.output, 'w', newline='') as f:
            writer = csv.DictWriter(f, fieldnames=['timestamp', 'bandwidth_gbps', 'phase'])
            writer.writeheader()
            writer.writerows(samples)
        
        print("-" * 60)
        print(f"[Client] Results saved to: {args.output}")
        print(f"[Client] Total samples: {len(samples)}")
        
        # 简单统计
        baseline = [s['bandwidth_gbps'] for s in samples if s['phase'] == 0]
        attack = [s['bandwidth_gbps'] for s in samples if s['phase'] == 1]
        
        if baseline:
            print(f"[Client] Baseline (0-5s):   {sum(baseline)/len(baseline):.2f} Gbps (avg)")
        if attack:
            attack_avg = sum(attack)/len(attack)
            print(f"[Client] Attack phase:      {attack_avg:.2f} Gbps (avg)")
            if baseline:
                degradation = (sum(baseline)/len(baseline) - attack_avg) / (sum(baseline)/len(baseline)) * 100
                print(f"[Client] Degradation:        {degradation:.1f}%")
    
    return 0


def main():
    parser = argparse.ArgumentParser(
        description='Victim Bandwidth Monitor for MR Deregistration Abuse Experiment'
    )
    parser.add_argument('--mode', choices=['server', 'client'], required=True,
                       help='Run as server or client')
    parser.add_argument('--device', default='mlx5_0',
                       help='RDMA device name (default: mlx5_0)')
    parser.add_argument('--port', type=int, default=18515,
                       help='Port number (default: 18515)')
    parser.add_argument('--server',
                       help='Server IP (required for client mode)')
    parser.add_argument('--duration', type=int, default=30,
                       help='Test duration in seconds (default: 30)')
    parser.add_argument('--size', type=int, default=65536,
                       help='Message size in bytes (default: 65536)')
    parser.add_argument('--output', default='victim_bandwidth.csv',
                       help='Output CSV file (default: victim_bandwidth.csv)')
    
    args = parser.parse_args()
    
    # 设置信号处理
    signal.signal(signal.SIGINT, signal_handler)
    
    if args.mode == 'server':
        return run_server(args)
    else:
        if not args.server:
            print("[ERROR] --server is required for client mode")
            parser.print_help()
            return 1
        return run_client(args)


if __name__ == '__main__':
    sys.exit(main())
