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
struct Bullet { float x, y; int ownerId; }; // 弾の持ち主IDを追加

int WINAPI WinMain(HINSTANCE h, HINSTANCE hp, LPSTR lp, int n) {
    WSADATA wsaData; WSAStartup(MAKEWORD(2, 2), &wsaData);

    // --- TCP接続確認 ---
    SOCKET tcpSock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in servAddrTcp = { AF_INET, htons(8888) };
    inet_pton(AF_INET, SERVER_IP, &servAddrTcp.sin_addr.s_addr);

    if (connect(tcpSock, (sockaddr*)&servAddrTcp, sizeof(servAddrTcp)) == SOCKET_ERROR) {
        MessageBox(NULL, "Server not found!", "Error", MB_OK);
        return -1;
    }

    // --- UDP準備 ---
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in servAddr = { AF_INET, htons(8889) };
    inet_pton(AF_INET, SERVER_IP, &servAddr.sin_addr.s_addr);
    unsigned long arg = 0x01; ioctlsocket(sock, FIONBIO, &arg);

    SetOutApplicationLogValidFlag(FALSE); // Log.txtを出さない
    ChangeWindowMode(TRUE); SetGraphMode(1280, 720, 32);
    DxLib_Init(); SetDrawScreen(DX_SCREEN_BACK);

    // 画像読み込み
    int imgBG = LoadGraph("Assets\\bg.png");
    int imgP = LoadGraph("Assets\\tiny_ship5.png");
    int imgE = LoadGraph("Assets\\EnemyZK_ship.png");

    // 弾の画像を配列化 (0:通常, 1:Pbullet2)
    int imgB[2];
    imgB[0] = LoadGraph("Assets\\Pbullet.png");
    imgB[1] = LoadGraph("Assets\\Pbullet2.png");

    int imgEB = LoadGraph("Assets\\Ebullete.png");

    std::vector<Entity> others;
    std::vector<Bullet> pBullets;
    int myX, myY, teamScore = 0, invinc = 0, survivedTime = 0;
    int myID = -1; // 自分のサーバー内ID
    int bgScrollY = 0;

    while (ProcessMessage() == 0 && !CheckHitKey(KEY_INPUT_ESCAPE)) {
        ClearDrawScreen();

        // 背景スクロール描画
        DrawGraph(0, bgScrollY, imgBG, FALSE);
        DrawGraph(0, bgScrollY - 720, imgBG, FALSE);
        bgScrollY += 2; if (bgScrollY >= 720) bgScrollY = 0;

        GetMousePoint(&myX, &myY);
        if (invinc > 0) invinc--;

        // 1. 座標送信
        PLAYER_DATA sd = { (int)htonl(myX), (int)htonl(myY), 0, (int)htonl(0) };
        sendto(sock, (char*)&sd, sizeof(sd), 0, (sockaddr*)&servAddr, sizeof(servAddr));

        // 2. 受信処理 (スコア、時間、他プレイヤー、敵、敵弾)
        PLAYER_DATA rb[1024];
        int ret;
        while ((ret = recvfrom(sock, (char*)rb, sizeof(rb), 0, NULL, NULL)) > 0) {
            int num = ret / sizeof(PLAYER_DATA);
            std::vector<Entity> nextOthers;
            for (int i = 0; i < num; i++) {
                int type = ntohl(rb[i].type);

                if (type == 10) { // スコア・時間パケット
                    teamScore = ntohl(rb[i].x);
                    survivedTime = ntohl(rb[i].y);
                }
                else if (type == 0) { // プレイヤーデータ
                    Entity e;
                    e.tarX = (float)ntohl(rb[i].x); e.tarY = (float)ntohl(rb[i].y);
                    e.id = ntohl(rb[i].angle); e.type = 0;

                    // 自分のIDを確定させる
                    if (myID == -1 && abs(e.tarX - myX) < 2 && abs(e.tarY - myY) < 2) myID = e.id;

                    if (e.id != myID) {
                        e.curX = e.tarX; e.curY = e.tarY;
                        for (auto& old : others) { if (old.id == e.id && old.type == 0) { e.curX = old.curX; e.curY = old.curY; break; } }
                        nextOthers.push_back(e);
                    }
                }
                else { // 敵(1)・敵弾(2)
                    Entity e;
                    e.tarX = (float)ntohl(rb[i].x); e.tarY = (float)ntohl(rb[i].y);
                    e.id = ntohl(rb[i].angle); e.type = type;
                    nextOthers.push_back(e);
                }
            }
            others = nextOthers;
        }

        // 3. 自機弾の生成
        static int ls = 0;
        if ((GetMouseInput() & MOUSE_INPUT_LEFT) && GetNowCount() - ls > 150) {
            pBullets.push_back({ (float)myX, (float)myY, myID });
            ls = GetNowCount();
        }

        // 4. 自機弾の移動・描画・敵への当たり判定
        for (auto bit = pBullets.begin(); bit != pBullets.end(); ) {
            bit->y -= 15.0f; bool hit = false;
            for (auto& e : others) {
                if (e.type == 1 && hypot(bit->x - e.tarX, bit->y - e.tarY) < 40) {
                    // サーバーへ敵撃破を通知
                    PLAYER_DATA k = { (int)htonl(e.id), 0, 0, (int)htonl(9) };
                    sendto(sock, (char*)&k, sizeof(k), 0, (sockaddr*)&servAddr, sizeof(servAddr));
                    hit = true; break;
                }
            }
            if (hit || bit->y < -50) bit = pBullets.erase(bit);
            else {
                // IDによって弾の色を変える
                int bIdx = (bit->ownerId == -1) ? 0 : (bit->ownerId % 2);
                DrawGraph((int)bit->x - 16, (int)bit->y - 16, imgB[bIdx], TRUE);
                ++bit;
            }
        }

        // 5. 他プレイヤー・敵・敵弾の描画と自分への当たり判定
        for (auto& e : others) {
            if (e.type == 2) { // 敵弾
                DrawGraph((int)e.tarX - 16, (int)e.tarY - 16, imgEB, TRUE);
            }
            else if (e.type == 1) { // 敵
                DrawGraph((int)e.tarX - 32, (int)e.tarY - 32, imgE, TRUE);
            }
            else if (e.type == 0) { // 他プレイヤー
                e.curX += (e.tarX - e.curX) * 0.2f; e.curY += (e.tarY - e.curY) * 0.2f;
                DrawGraph((int)e.curX - 32, (int)e.curY - 32, imgP, TRUE);
            }

            // 被弾判定
            if (invinc == 0) {
                float d = hypot((float)myX - e.tarX, (float)myY - e.tarY);
                if ((e.type == 1 && d < 35) || (e.type == 2 && d < 15)) {
                    invinc = 120;
                    // サーバーへ被弾を通知 (スコア減算のため)
                    PLAYER_DATA dmg = { 0, 0, 0, (int)htonl(8) };
                    sendto(sock, (char*)&dmg, sizeof(dmg), 0, (sockaddr*)&servAddr, sizeof(servAddr));
                }
            }
        }

        // 6. 自機の点滅描画
        if (invinc == 0 || (invinc % 24 < 12)) {
            DrawGraph(myX - 32, myY - 32, imgP, TRUE);
        }

        // UI表示
        DrawFormatString(1000, 20, GetColor(255, 255, 0), "TEAM SCORE: %d", teamScore);
        DrawFormatString(1000, 40, GetColor(255, 255, 255), "TIME: %02d:%02d", survivedTime / 60, survivedTime % 60);
        if (invinc > 0) DrawString(myX - 40, myY + 40, "TEAM DAMAGE!", GetColor(255, 0, 0));

        ScreenFlip();
    }

    closesocket(tcpSock);
    DxLib_End(); return 0;
}