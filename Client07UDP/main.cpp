#define WIN32_LEAN_AND_MEAN
#include "DxLib.h"
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <cmath>

#pragma comment(lib, "ws2_32.lib")

const char* SERVER_IP = "192.168.42.174";

struct PLAYER_DATA { int x, y, angle, type; };
struct Entity { float tarX, tarY; int id, type; };

int WINAPI WinMain(HINSTANCE h, HINSTANCE hp, LPSTR lp, int n) {
    WSADATA wsaData; WSAStartup(MAKEWORD(2, 2), &wsaData);

    SOCKET tcpSock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in servAddrTcp = { AF_INET, htons(8888) };
    inet_pton(AF_INET, SERVER_IP, &servAddrTcp.sin_addr.s_addr);
    if (connect(tcpSock, (sockaddr*)&servAddrTcp, sizeof(servAddrTcp)) == SOCKET_ERROR) return -1;

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in servAddr = { AF_INET, htons(8889) };
    inet_pton(AF_INET, SERVER_IP, &servAddr.sin_addr.s_addr);
    unsigned long arg = 0x01; ioctlsocket(sock, FIONBIO, &arg);

    SetOutApplicationLogValidFlag(FALSE);
    ChangeWindowMode(FALSE); // フルスクリーン
    SetGraphMode(1920, 1080, 32);
    if (DxLib_Init() == -1) return -1;
    SetDrawScreen(DX_SCREEN_BACK);

    int imgBG = LoadGraph("Assets\\bg.png"), imgP = LoadGraph("Assets\\tiny_ship5.png"), imgE = LoadGraph("Assets\\EnemyZK_ship.png");
    int imgB[2] = { LoadGraph("Assets\\Pbullet.png"), LoadGraph("Assets\\Pbullet2.png") };
    int imgEB = LoadGraph("Assets\\Ebullete.png");

    std::vector<Entity> entities;
    int myX, myY, teamScore = 0, invinc = 0, survivedTime = 0, myID = -1;
    float bgScrollY = 0;

    while (ProcessMessage() == 0 && !CheckHitKey(KEY_INPUT_ESCAPE)) {
        ClearDrawScreen();
        DrawExtendGraph(0, (int)bgScrollY, 1920, (int)bgScrollY + 1080, imgBG, FALSE);
        DrawExtendGraph(0, (int)bgScrollY - 1080, 1920, (int)bgScrollY, imgBG, FALSE);
        if (survivedTime < 60) { bgScrollY += 3.0f; if (bgScrollY >= 1080.0f) bgScrollY = 0; }

        GetMousePoint(&myX, &myY);
        if (invinc > 0) invinc--;

        if (survivedTime < 60) {
            PLAYER_DATA sd = { (int)htonl(myX), (int)htonl(myY), 0, (int)htonl(0) };
            sendto(sock, (char*)&sd, sizeof(sd), 0, (sockaddr*)&servAddr, sizeof(servAddr));

            static int ls = 0;
            if ((GetMouseInput() & MOUSE_INPUT_LEFT) && GetNowCount() - ls > 150) {
                PLAYER_DATA shoot = { (int)htonl(myX), (int)htonl(myY), 0, (int)htonl(7) };
                sendto(sock, (char*)&shoot, sizeof(shoot), 0, (sockaddr*)&servAddr, sizeof(servAddr));
                ls = GetNowCount();
            }
        }

        PLAYER_DATA rb[4096];
        int ret;
        while ((ret = recvfrom(sock, (char*)rb, sizeof(rb), 0, NULL, NULL)) > 0) {
            int num = ret / sizeof(PLAYER_DATA);
            std::vector<Entity> nextEntities;
            for (int i = 0; i < num; i++) {
                int type = ntohl(rb[i].type);
                if (type == 10) { teamScore = ntohl(rb[i].x); survivedTime = ntohl(rb[i].y); }
                else {
                    Entity e = { (float)ntohl(rb[i].x), (float)ntohl(rb[i].y), ntohl(rb[i].angle), type };
                    if (type == 0 && myID == -1 && abs(e.tarX - myX) < 10 && abs(e.tarY - myY) < 10) myID = e.id;
                    nextEntities.push_back(e);
                }
            }
            entities = nextEntities;
        }

        // 4. エンティティ描画
        for (auto& e : entities) {
            if (e.tarY > 1080 || e.tarY < -110) continue;

            if (e.type == 3) DrawGraph((int)e.tarX - 16, (int)e.tarY - 16, imgB[e.id % 2], TRUE);
            else if (e.type == 2) DrawGraph((int)e.tarX - 16, (int)e.tarY - 16, imgEB, TRUE);
            else if (e.type == 1) DrawGraph((int)e.tarX - 32, (int)e.tarY - 32, imgE, TRUE);
            else if (e.type == 0 && e.id != myID) DrawGraph((int)e.tarX - 32, (int)e.tarY - 32, imgP, TRUE);

            if (survivedTime < 60 && invinc == 0 && (e.type == 1 || e.type == 2)) {
                if (hypot((float)myX - e.tarX, (float)myY - e.tarY) < (e.type == 1 ? 40 : 15)) {
                    invinc = 120;
                    PLAYER_DATA dmg = { 0, 0, 0, (int)htonl(8) };
                    sendto(sock, (char*)&dmg, sizeof(dmg), 0, (sockaddr*)&servAddr, sizeof(servAddr));
                }
            }
        }

        // 5. 自機
        if (survivedTime < 60 && (invinc == 0 || (invinc % 24 < 12))) {
            DrawGraph(myX - 32, myY - 32, imgP, TRUE);
        }

        // ★追加：DAMAGE!の表示
        if (invinc > 0 && survivedTime < 60) {
            // 文字を少し太く大きく見せるために位置をずらして重ね書き（簡易太字）
            DrawString(myX - 45, myY + 40, "DAMAGE!", GetColor(255, 0, 0));
            DrawString(myX - 44, myY + 41, "DAMAGE!", GetColor(255, 0, 0));
        }

        // 6. UI表示
        DrawFormatString(1600, 40, GetColor(255, 255, 0), "SCORE: %d", teamScore);
        DrawFormatString(1600, 70, GetColor(255, 255, 255), "TIME: %02d / 60", survivedTime);

        if (survivedTime >= 60) {
            DrawBox(760, 450, 1160, 630, GetColor(0, 0, 0), TRUE);
            DrawBox(760, 450, 1160, 630, GetColor(255, 255, 255), FALSE);
            DrawFormatString(840, 520, GetColor(255, 255, 255), "FINAL SCORE: %d", teamScore);
            DrawString(890, 480, "TIME UP!", GetColor(255, 0, 0));
        }

        ScreenFlip();
    }
    closesocket(tcpSock); DxLib_End(); return 0;
}