﻿/*
 * Touch+ Software
 * Copyright (C) 2015
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the Aladdin Free Public License as
 * published by the Aladdin Enterprises, either version 9 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * Aladdin Free Public License for more details.
 *
 * You should have received a copy of the Aladdin Free Public License
 * along with this program.  If not, see <http://ghostscript.com/doc/8.54/Public.htm>.
 */

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;

namespace Assets
{
    class IPC
    {
        public static bool Updated = true;
        public static Dictionary<string, bool> FileNameProcessedMap = new Dictionary<string, bool>();
        public static int SentCount = 0;

        private string selfName = "";
        private Dictionary<string, Func<string, int>> responseMap = new Dictionary<string, Func<string, int>>();
        private Dictionary<string, Func<string, int>> commandMap = new Dictionary<string, Func<string, int>>();
        private UDP udp = new UDP();

        public IPC(string selfNameIn)
        {
            selfName = selfNameIn;

            MapFunction("open udp channel", delegate(string messageBody)
            {
                if (messageBody == "")
                {
                    int port = udp.Assign();
                    SendMessage("track_plus", "open udp channel", port.ToString());
                    Globals.WriteLine("bound to UDP port " + port);
                }
                else
                {
                    int port;
                    int.TryParse(messageBody, out port);
                    udp.Assign(port);
                    Globals.WriteLine("bound to UDP port " + port);
                }
                return 1;
            });

            Update();
        }

        public void Update()
        {
            if (!IPC.Updated)
                return;

            IPC.Updated = false;

            List<string> fileNameVec = FileSystem.ListFilesInDirectory(Globals.IpcPath);
            foreach (string fileNameCurrent in fileNameVec)
            {
                string fileNameEveryone = "";
                if (fileNameCurrent.Length >= 8)
                    fileNameEveryone = fileNameCurrent.Substring(0, 8);

                if (fileNameCurrent.Length > selfName.Length || fileNameEveryone == "everyone")
                {
                    if (IPC.FileNameProcessedMap.ContainsKey(fileNameCurrent) && IPC.FileNameProcessedMap[fileNameCurrent] == true)
                        continue;
                    else
                        IPC.FileNameProcessedMap[fileNameCurrent] = true;

                    string fileName = "";
                    string fileNameID = "";
                    if (fileNameEveryone != "everyone")
                    {
                        fileName = fileNameCurrent.Substring(0, selfName.Length);
                        fileNameID = fileNameCurrent.Substring(selfName.Length, fileNameCurrent.Length - selfName.Length);
                    }

                    if (fileName == selfName || fileNameEveryone == "everyone")
                    {
                        Thread.Sleep(20);

                        List<string> lines = FileSystem.ReadTextFile(Globals.IpcPath + "\\" + fileNameCurrent);
                        // FileSystem.DeleteFile(Globals.IpcPath + "\\" + fileNameCurrent);
                        
                        string[] messageVec = lines[0].Split('!');
                        string messageHead = messageVec[0];
                        string messageBody = messageVec[1];

                        Globals.WriteLine("message received " + " " + messageHead + " " + messageBody + " " + fileNameCurrent);

                        if (!responseMap.ContainsKey(messageHead))
                        {
                            if (commandMap.ContainsKey(messageHead))
                                commandMap[messageHead](messageBody);
                        }
                        else
                        {
                            Func<string, int> func = responseMap[messageHead];
                            responseMap.Remove(messageHead);
                            func(messageBody);
                        }
                    }
                }
            }

            IPC.Updated = true;
        }

        public void Clear()
        {
            List<string> fileNameVec = FileSystem.ListFilesInDirectory(Globals.IpcPath);
            foreach (string fileNameCurrent in fileNameVec)
            {
                string fileNameEveryone = "";
                if (fileNameCurrent.Length >= 8)
                    fileNameEveryone = fileNameCurrent.Substring(0, 8);

                if (fileNameCurrent.Length > selfName.Length || fileNameEveryone == "everyone")
                {
                    string fileName = "";
                    string fileNameID = "";
                    if (fileNameEveryone != "everyone")
                    {
                        fileName = fileNameCurrent.Substring(0, selfName.Length);
                        fileNameID = fileNameCurrent.Substring(selfName.Length, fileNameCurrent.Length - selfName.Length);
                    }
                    else
                        continue;

                    if (fileName == selfName || fileNameEveryone == "everyone")
                        FileSystem.DeleteFile(Globals.IpcPath + "\\" + fileNameCurrent);
                }
            }
        }

        public void SendMessage(string recipient, string messageHead, string messageBody)
        {
            List<string> fileNameVec = FileSystem.ListFilesInDirectory(Globals.IpcPath);

            bool found = true;
            int fileCount = 0;

            while (found)
            {
                found = false;
                foreach (string fileNameCurrent in fileNameVec)
                    if (fileNameCurrent == recipient + fileCount.ToString())
                    {
                        found = true;
                        ++fileCount;
                        break;
                    }
            }

            string pathOld = Globals.IpcPath + "\\s" + selfName + IPC.SentCount.ToString();
            string pathNew = Globals.IpcPath + "\\" + recipient + fileCount.ToString();

            FileSystem.WriteStringToFile(pathOld, messageHead + "!" + messageBody);
            FileSystem.RenameFile(pathOld, pathNew);

            ++IPC.SentCount;

            Globals.WriteLine("message sent: " + recipient + " " + messageHead + " " + messageBody);
        }

        public void GetResponse(string recipient, string messageHead, string messageBody, Func<string, int> callback)
        {
            SendMessage(recipient, messageHead, messageBody);
            responseMap[messageHead] = callback;
        }

        public void MapFunction(string messageHead, Func<string, int> callback)
        {
            commandMap[messageHead] = callback;
        }

        public void SetUDPCallback(Func<string, int> udpCallbackIn)
        {
            udp.SetUDPCallback(udpCallbackIn);
        }
    }
}
