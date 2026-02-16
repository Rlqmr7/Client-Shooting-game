#define WIN32_LEAN_AND_MEAN
#include "DxLib.h"
#include <WinSock2.h>
#include <ws2tcpip.h>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

struct PLAYER_DATA {
    int x, y, angle, type;
};

// 描画を滑らかにするための構造体
struct Entity {
    float curX, curY; // 現在の描画位置
    float tarX, tarY; // サーバーから届いた目標位置
};

const char* SERVER_IP = "127.0.0.1";
const int SERVER_PORT = 8888;

int WINAPI WinMain(HINSTANCE h, HINSTANCE hp, LPSTR lp, int n) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // UDPソケット
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in servAddr = { AF_INET, htons(SERVER_PORT) };
    inet_pton(AF_INET, SERVER_IP, &servAddr.sin_addr.s_addr);

    unsigned long arg = 0x01;
    ioctlsocket(sock, FIONBIO, &arg);

    ChangeWindowMode(TRUE);
    if (DxLib_Init() == -1) return -1;
    SetDrawScreen(DX_SCREEN_BACK);

    int playerImg = LoadGraph("Assets\\tiny_ship5.png");
    if (playerImg == -1) {
        MessageBox(NULL, "画像読み込みに失敗しました！パスを確認してください。", "Error", MB_OK);
        return -1;
    }
    std::vector<Entity> others;
    int myX = 400, myY = 300;

    while (ProcessMessage() == 0 && !CheckHitKey(KEY_INPUT_ESCAPE)) {
        ClearDrawScreen();

        // 1. 入力と送信 (30fps程度に制限)
        GetMousePoint(&myX, &myY);
        static int lastSend = 0;
        if (GetNowCount() - lastSend > 30) {
            PLAYER_DATA sd = { (int)htonl(myX), (int)htonl(myY), 0, 0 };
            sendto(sock, (char*)&sd, sizeof(sd), 0, (sockaddr*)&servAddr, sizeof(servAddr));
            lastSend = GetNowCount();
        }

        // 2. 受信
        PLAYER_DATA rb[10];
        int ret = recvfrom(sock, (char*)rb, sizeof(rb), 0, NULL, NULL);
        if (ret > 0) {
            int num = ret / sizeof(PLAYER_DATA);
            if (others.size() != num) others.resize(num);
            for (int i = 0; i < num; i++) {
                others[i].tarX = (float)ntohl(rb[i].x);
                others[i].tarY = (float)ntohl(rb[i].y);
                // 初回のみ位置を同期
                if (others[i].curX == 0) { others[i].curX = others[i].tarX; others[i].curY = others[i].tarY; }
            }
        }

        // 3. 補間と描画
        for (auto& e : others) {
            // 線形補間: 現在地を目標地に20%ずつ近づける (滑らかに見えるコツ)
            e.curX += (e.tarX - e.curX) * 0.2f;
            e.curY += (e.tarY - e.curY) * 0.2f;
            DrawGraph((int)e.curX - 32, (int)e.curY - 32, playerImg, TRUE);
        }

        ScreenFlip();
    }

    closesocket(sock);
    WSACleanup();
    DxLib_End();
    return 0;
}