#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <vector>
#include <string>
#include <mutex>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <map>
#include <sstream>
#include <fmod.hpp>
#include <fmod_errors.h>
#define _USE_MATH_DEFINES
#include <cmath>
#pragma comment(lib, "fmod_vc.lib")
#pragma comment(lib, "ws2_32.lib")


FMOD::System* fSystem = NULL;
FMOD::Sound* pChatSound = NULL;
FMOD::Sound* pDMSound = NULL;
FMOD::Channel* channel = NULL;

FMOD_VECTOR playerPos = { 0.0f, 0.0f, 0.0f };
FMOD_VECTOR playerVel = { 0.0f, 0.0f, 0.0f };
FMOD_VECTOR playerForward = { 0.0f, 0.0f, 1.0f };
FMOD_VECTOR playerUp = { 0.0f, 1.0f, 0.0f };
FMOD_VECTOR soundPos = { 5.0f, 0.0f, 0.0f };
FMOD_VECTOR soundVel = { 0.0f, 0.0f, 0.0f };

static SOCKET client_socket = INVALID_SOCKET;
static bool isConnected = false;
static std::mutex chatMutex;

static char Nickname[64] = "Spring";
static char inputBuf[256] = "";
static std::vector<std::string> chatHistory;
static std::vector<std::string> onlineUsers;
static std::map<std::string, std::vector<std::string>> dmMsg;
static std::map<std::string, bool> dmWindow;

void ReceiveWorker() {
    char buf[1024];
    while (isConnected) {
        int bytes = recv(client_socket, buf, sizeof(buf) - 1, 0);

        if (bytes > 0) {
            buf[bytes] = '\0';
            std::string msg(buf);

            chatMutex.lock();

            if (msg.substr(0, 5) == "LIST:") {
                onlineUsers.clear();
                std::stringstream ss(msg.substr(5));
                std::string name;
                while (std::getline(ss, name, ',')) {
                    if (!name.empty()) onlineUsers.push_back(name);
                }
            }
            else if (msg.find("[DM from ") != std::string::npos) {
                size_t start = msg.find("from ") + 5;
                size_t end = msg.find("]:");
                std::string sender = msg.substr(start, end - start);

                dmWindow[sender] = true;
                dmMsg[sender].push_back(msg);

                fSystem->playSound(pDMSound, NULL, true, &channel);
                channel->set3DAttributes(&soundPos, &soundVel);
                channel->setPaused(false);
            }
            else {
                chatHistory.push_back(msg);
                fSystem->playSound(pChatSound, NULL, true, &channel);
                channel->set3DAttributes(&soundPos, &soundVel);
                channel->setPaused(false);
            }
            chatMutex.unlock();
        }
        else {
            isConnected = false;
            break;
        }
    }
}


void InitAudio() {
    FMOD::System_Create(&fSystem);
    fSystem->setSoftwareFormat(0, FMOD_SPEAKERMODE_STEREO, 0);
    fSystem->init(512, FMOD_INIT_NORMAL, NULL);
    fSystem->set3DSettings(1.0f, 1.0f, 1.0f);
    fSystem->set3DListenerAttributes(0, &playerPos, &playerVel, &playerForward, &playerUp);
    fSystem->createSound("chat.wav", FMOD_3D, NULL, &pChatSound);
    pChatSound->setMode(FMOD_LOOP_OFF);
    fSystem->createSound("dm.wav", FMOD_3D, NULL, &pDMSound);
    pDMSound->setMode(FMOD_LOOP_OFF);
}
void InitNetwork() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return;

    client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket == INVALID_SOCKET) 
    { 
        WSACleanup(); 
        return; 
    }

    sockaddr_in server_address = {};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(65432);
    inet_pton(AF_INET, "127.0.0.1", &server_address.sin_addr);

    if (connect(client_socket, (sockaddr*)&server_address, sizeof(server_address)) != SOCKET_ERROR) {
        isConnected = true;

        std::string loginMsg = "LOGIN:" + std::string(Nickname);
        send(client_socket, loginMsg.c_str(), (int)loginMsg.size(), 0);

        std::thread(ReceiveWorker).detach();
    }
}

// Data
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool                     g_SwapChainOccluded = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int main(int, char**)
{
    // Make process DPI aware and obtain main monitor scale
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    // Create application window
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Dear ImGui DirectX11 Example", WS_OVERLAPPEDWINDOW, 100, 100, (int)(1280 * main_scale), (int)(800 * main_scale), nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (using io.ConfigDpiScaleFonts=true makes this unnecessary. We leave both here for documentation purpose)

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    InitAudio();

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    // Main loop
    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
            {
                done = true;
            }
        }

        if (fSystem) fSystem->update();

        if (done) 
            break;

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        // Handle window resize (we don't resize directly in the WM_SIZE handler)
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        if (!isConnected)
        {
            ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(350, 180), ImGuiCond_FirstUseEver);
            ImGui::Begin("Login Page");
            {
                ImGui::Text("Hi!Please enter your nickname:");
                ImGui::Spacing();
                ImGui::InputText("Nickname", Nickname, 64);
                ImGui::Spacing();
                if (ImGui::Button("Connect", ImVec2(-1, 40))) {
                    InitNetwork();
                }
            }
            ImGui::End();
        }
        else
        {
            ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(180, 600), ImGuiCond_FirstUseEver);
            ImGui::Begin("Online Users", nullptr, ImGuiWindowFlags_NoResize);
            {
                chatMutex.lock();
                ImGui::Text("Users:");
                ImGui::Separator();
                for (auto& user : onlineUsers) {
                    if (user != Nickname) {
                        if (ImGui::Selectable(user.c_str())) {
                            dmWindow[user] = true;
                        }
                    }
                    else {
                        ImGui::TextDisabled("%s (You)", user.c_str());
                    }
                }
                chatMutex.unlock();
            }
            ImGui::End();

            ImGui::SetNextWindowPos(ImVec2(200, 10), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);
            ImGui::Begin("Public Chatroom");
            {
                ImGui::Text("You are: %s", Nickname);
                ImGui::Separator();
                float footer_height = ImGui::GetFrameHeightWithSpacing() * 2.5f;
                ImGui::BeginChild("MainScroll", ImVec2(0, -footer_height), true);
                {
                    chatMutex.lock();
                    for (const auto& msg : chatHistory) {
                        ImGui::TextUnformatted(msg.c_str());
                    }
                    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
                    chatMutex.unlock();
                }
                ImGui::EndChild();
                ImGui::Separator();



                static char mainInput[256] = "";
                ImGui::PushItemWidth(-100);
                ImGui::InputText("##PubInput", mainInput, 256);
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if ((ImGui::Button("Send")) && mainInput[0] != '\0') {
                    std::string out = "PUB:" + std::string(mainInput);
                    send(client_socket, out.c_str(), (int)out.size(), 0);
                    mainInput[0] = '\0';
                }
            }
            ImGui::End();

            for (auto& DmSi : dmWindow) {
                if (DmSi.second) {
                    ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
                    if (ImGui::Begin(DmSi.first.c_str(), &DmSi.second)) {
                        ImGui::BeginChild("DMScroll", ImVec2(0, -45), true);
                        {
                            chatMutex.lock();
                            for (auto& m : dmMsg[DmSi.first]) ImGui::TextUnformatted(m.c_str());
                            chatMutex.unlock();
                        }
                        ImGui::EndChild();
                        static char dmInput[256] = "";
                        ImGui::PushItemWidth(-100);
                        ImGui::InputText("##dmInput", dmInput, 256);
                        ImGui::PopItemWidth();
                        ImGui::SameLine();
                        if ((ImGui::Button("Send")) && dmInput[0] != '\0') {
                            std::string out = "DM:" + DmSi.first + ":" + std::string(dmInput);
                            send(client_socket, out.c_str(), (int)out.size(), 0);
                            chatMutex.lock();
                            dmMsg[DmSi.first].push_back("[To " + DmSi.first + "]: " + dmInput);
                            chatMutex.unlock();
                            dmInput[0] = '\0';
                        }
                    }
                    ImGui::End();
                }
            }
        }

        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        // Present
        HRESULT hr = g_pSwapChain->Present(1, 0);   // Present with vsync
        //HRESULT hr = g_pSwapChain->Present(0, 0); // Present without vsync
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    if (pChatSound) pChatSound->release();
    if (pDMSound) pDMSound->release();
    if (fSystem) {
        fSystem->close();
        fSystem->release();
    }

    return 0;
}

// Helper functions

bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    // This is a basic setup. Optimally could use e.g. DXGI_SWAP_EFFECT_FLIP_DISCARD and handle fullscreen mode differently. See #8979 for suggestions.
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
