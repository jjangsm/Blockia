using Google.Protobuf;
using Protocol;
using System;
using UnityEngine;

public class SendAuth : ValueComponent
{
    private string gmail = string.Empty;
    private string authCode = string.Empty;
    public override void Activate()
    {
        gmail = textValues.Dequeue();
        authCode = textValues.Dequeue();
    }

    public void TestPacket()
    {
        uint header = (uint)PACKET_HEADER.PktReqSignUp;
        LoginData data = new()
        {
            Id = "TEST1234",
            Mail = "TestMail@gmail.com",
            Password = "1234567890",
        };

        byte[] msg = data.ToByteArray();
        ushort size = (ushort)(msg.Length);
        byte[] buffer = new byte[size + 6];

        Buffer.BlockCopy(BitConverter.GetBytes(size), 0, buffer, 0, 2);
        Buffer.BlockCopy(BitConverter.GetBytes(header), 0, buffer, 2, 4);
        Buffer.BlockCopy(msg, 0, buffer, 6, size);
        NetCore.Instance.Enqueue(buffer);
    }
}
