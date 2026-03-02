#define WIN32_LEAN_AND_MEAN
#include "DxLib.h"
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <cmath>

#pragma comment(lib, "ws2_32.lib")

// ★ここにサーバーのIPを入れる★
const char* SERVER_IP = "192.168.42.174";

struct PLAYER_DATA { int x, y, angle, type; };
struct Entity { float curX, curY, tarX, tarY; int id, type; };
struct Bullet { float x, y; };

int WINAPI WinMain(HINSTANCE h, HINSTANCE hp, LPSTR lp, int n) {
    WSADATA wsaData; WSAStartup(MAKEWORD(2, 2), &wsaData);

    // --- 【追加】TCPで接続確認を行う ---
    SOCKET tcpSock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in servAddrTcp = { AF_INET, htons(8888) }; // TCPは8888番
    inet_pton(AF_INET, SERVER_IP, &servAddrTcp.sin_addr.s_addr);

    printf("Connecting to Server...\n");
    if (connect(tcpSock, (sockaddr*)&servAddrTcp, sizeof(servAddrTcp)) == SOCKET_ERROR) {
        MessageBox(NULL, "Server not found! (TCP Connection Failed)", "Error", MB_OK);
        return -1;
    }
    // ----------------------------------

    // UDPの準備 (ポートは8889に変更)
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in servAddr = { AF_INET, htons(8889) };
    inet_pton(AF_INET, SERVER_IP, &servAddr.sin_addr.s_addr);
    unsigned long arg = 0x01; ioctlsocket(sock, FIONBIO, &arg);

    ChangeWindowMode(TRUE); SetGraphMode(1280, 720, 32);
    DxLib_Init(); SetDrawScreen(DX_SCREEN_BACK);

    int imgBG = LoadGraph("Assets\\bg.png");
    int imgP = LoadGraph("Assets\\tiny_ship5.png"), imgE = LoadGraph("Assets\\EnemyZK_ship.png");
    int imgB = LoadGraph("Assets\\Pbullet.png"), imgEB = LoadGraph("Assets\\Ebullete.png");

    std::vector<Entity> others;
    std::vector<Bullet> pBullets;
    int myX, myY, score = 0, invinc = 0;
    int bgScrollY = 0;

    while (ProcessMessage() == 0 && !CheckHitKey(KEY_INPUT_ESCAPE)) {
        ClearDrawScreen();
        DrawGraph(0, bgScrollY, imgBG, FALSE);
        DrawGraph(0, bgScrollY - 720, imgBG, FALSE);

        GetMousePoint(&myX, &myY);
        if (invinc > 0) invinc--;

        // 1. 送信
        PLAYER_DATA sd = { (int)htonl(myX), (int)htonl(myY), 0, (int)htonl(0) };
        sendto(sock, (char*)&sd, sizeof(sd), 0, (sockaddr*)&servAddr, sizeof(servAddr));

        // 2. 受信
        PLAYER_DATA rb[512];
        int ret;
        while ((ret = recvfrom(sock, (char*)rb, sizeof(rb), 0, NULL, NULL)) > 0) {
            int num = ret / sizeof(PLAYER_DATA);
            std::vector<Entity> nextOthers;
            for (int i = 0; i < num; i++) {
                Entity e;
                e.tarX = (float)ntohl(rb[i].x); e.tarY = (float)ntohl(rb[i].y);
                e.id = ntohl(rb[i].angle); e.type = ntohl(rb[i].type);
                if (e.tarX == 0 && e.tarY == 0) continue;
                if (e.type == 0 && hypot(e.tarX - myX, e.tarY - myY) < 50) continue;

                e.curX = e.tarX; e.curY = e.tarY;
                for (auto& old : others) {
                    if (old.type == e.type && old.id == e.id) { e.curX = old.curX; e.curY = old.curY; break; }
                }
                nextOthers.push_back(e);
            }
            others = nextOthers;
        }

        // 3. 自機弾
        static int ls = 0;
        if ((GetMouseInput() & MOUSE_INPUT_LEFT) && GetNowCount() - ls > 150) {
            pBullets.push_back({ (float)myX, (float)myY }); ls = GetNowCount();
        }
        for (auto bit = pBullets.begin(); bit != pBullets.end(); ) {
            bit->y -= 15.0f; bool hit = false;
            for (auto& e : others) {
                if (e.type == 1 && hypot(bit->x - e.tarX, bit->y - e.tarY) < 40) {
                    PLAYER_DATA k = { (int)htonl(e.id), 0, 0, (int)htonl(9) };
                    sendto(sock, (char*)&k, sizeof(k), 0, (sockaddr*)&servAddr, sizeof(servAddr));
                    score += 100; hit = true; break;
                }
            }
            if (hit || bit->y < -50) bit = pBullets.erase(bit);
            else { DrawGraph((int)bit->x - 16, (int)bit->y - 16, imgB, TRUE); ++bit; }
        }

        // 4. 描画と被弾判定
        for (auto& e : others) {
            if (e.type == 2) {
                DrawGraph((int)e.tarX - 16, (int)e.tarY - 16, imgEB, TRUE);
            }
            else {
                e.curX += (e.tarX - e.curX) * 0.2f; e.curY += (e.tarY - e.curY) * 0.2f;
                DrawGraph((int)e.curX - 32, (int)e.curY - 32, (e.type == 1 ? imgE : imgP), TRUE);
            }
            if (invinc == 0) {
                float d = hypot((float)myX - e.tarX, (float)myY - e.tarY);
                if ((e.type == 1 && d < 35) || (e.type == 2 && d < 15)) {
                    invinc = 120; score -= 500; if (score < 0) score = 0;
                }
            }
        }

        // 5. 点滅描画
        if (invinc == 0 || (invinc % 24 < 12)) {
            DrawGraph(myX - 32, myY - 32, imgP, TRUE);
        }

        DrawFormatString(1100, 20, GetColor(255, 255, 0), "SCORE: %d", score);
        if (invinc > 0) DrawString(myX - 40, myY + 40, "-500 pts!", GetColor(255, 0, 0));

        bgScrollY += 2;
        if (bgScrollY >= 720) bgScrollY = 0;

        ScreenFlip();
    }

    closesocket(tcpSock); // 使い終わったら閉じる
    DxLib_End(); return 0;
}