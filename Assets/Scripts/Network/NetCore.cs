using Cysharp.Threading.Tasks;
using System;
using System.Collections.Generic;
using System.Net.Sockets;
using System.Text;
using UnityEngine;
using Google.Protobuf;
using Protocol;

public class NetCore : MonoBehaviour
{
    [Header("Server Info")]
    public string ip = "127.0.0.1";
    public int port = 8080;

    [Header("Test Settings")]
    public int clientCount = 10;

    [Header("Send Settings")]
    public float sendInterval = 1.0f;   // n초
    public int packetSize = 100;        // m bytes
    public bool send = false;

    private readonly List<Socket> clients = new();

    async UniTask ConnectClientsAsync()
    {
        for (int i = 0; i < clientCount; i++)
        {
            try
            {
                Socket sock = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
                await sock.ConnectAsync(ip, port);

                clients.Add(sock);
                Debug.Log($"Client {i} connected");

                // 🔥 각 클라마다 송신 루프 시작
                SendLoop(sock).Forget();
            }
            catch (SocketException e)
            {
                Debug.LogError($"Client {i} failed: {e.Message}");
            }

            await UniTask.Yield(); // 프레임 분산
        }
    }

    async UniTaskVoid SendLoop(Socket sock)
    {
        // 더미 패킷 생성
        uint header = (uint)PACKET_HEADER.PktUnknown;
        byte[] msg = Encoding.UTF8.GetBytes("Hello");
        ushort size = (ushort)(6 + msg.Length);
        byte[] buffer = new byte[size];

        Buffer.BlockCopy(BitConverter.GetBytes(size), 0, buffer, 0, 2);
        Buffer.BlockCopy(BitConverter.GetBytes(header), 0, buffer, 2, 4);
        Buffer.BlockCopy(msg, 0, buffer, 6, msg.Length);

        while (sock.Connected)
        {
            if (send)
            {
                try
                {
                    await sock.SendAsync(buffer, SocketFlags.None);
                }
                catch (SocketException e)
                {
                    Debug.LogError($"Send failed: {e.Message}");
                    break;
                }
            }

            await UniTask.Delay((int)(sendInterval * 1000));
        }

        sock.Close();
    }

    void Start()
    {
        ConnectClientsAsync().Forget();
    }

    void OnApplicationQuit()
    {
        foreach (var sock in clients)
        {
            if (sock.Connected)
                sock.Close();
        }
    }
}