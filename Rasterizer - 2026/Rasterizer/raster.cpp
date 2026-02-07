#include <iostream>
#define _USE_MATH_DEFINES
#include <cmath>

#include "GamesEngineeringBase.h" // Include the GamesEngineeringBase header
#include <algorithm>
#include <chrono>

#include <cmath>
#include "matrix.h"
#include "colour.h"
#include "mesh.h"
#include "zbuffer.h"
#include "renderer.h"
#include "RNG.h"
#include "light.h"
#include "triangle.h"
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
// Main rendering function that processes a mesh, transforms its vertices, applies lighting, and draws triangles on the canvas.
// Input Variables:
// - renderer: The Renderer object used for drawing.
// - mesh: Pointer to the Mesh object containing vertices and triangles to render.
// - camera: Matrix representing the camera's transformation.
// - L: Light object representing the lighting parameters.

struct SceneTriangle {
    Vertex t[3];
    float ka, kd;
};

static std::vector<std::thread> pool;
static bool initialized = false;
static std::mutex mtx;
static std::condition_variable cv_start, cv_done;
static bool ready = false;
static int finished_count = 0;
static std::vector<std::vector<SceneTriangle>> Buckets;
static std::vector<SceneTriangle>* pTriList = nullptr;
static Renderer* pRenderer = nullptr;
static Light* pLight = nullptr;
static GamesEngineeringBase::Timer fpsTimer;

void FPS() {
    static float Time = 0.0f;
    static int Frame = 0;

    Time += fpsTimer.dt();
    Frame++;

    if (Time >= 1.0f) {
        float fps = static_cast<float>(Frame) / Time;
        std::cout << "\n Current FPS: " << fps << std::flush;

        Time = 0.0f;
        Frame = 0;
    }
}
void threadWork(int i, int Threads) {
    while (true) {
        int minY, maxY;
        {
            std::unique_lock<std::mutex> lock(mtx);
            while (!ready) {
                cv_start.wait(lock);
            }

            int h = pRenderer->canvas.getHeight();
            minY = i * (h / Threads);
            if (i == Threads - 1) maxY = h;
            else maxY = (i + 1) * (h / Threads);
        }
        const std::vector<SceneTriangle>& myBucket = Buckets[i];
        for (const SceneTriangle& triData : myBucket) {
            triangle tri(triData.t[0], triData.t[1], triData.t[2]);
            tri.draw(*pRenderer, *pLight, triData.ka, triData.kd, minY, maxY);
        }

        {
            std::lock_guard<std::mutex> lock(mtx);
            finished_count++;
            if (finished_count == Threads) {
                cv_done.notify_one();
            }
        }
        {
            std::unique_lock<std::mutex> lock(mtx);
            while (ready) {
                cv_start.wait(lock);
            }
        }
    }
}



void render(Renderer& renderer, std::vector<Mesh*>& scene, matrix& camera, Light& L) {
    const int Threads =16;                                                      // Threads
    int canvasH = renderer.canvas.getHeight();
    int Rows = canvasH / Threads;

    if (Buckets.size() != (size_t)Threads) {
        Buckets.resize(Threads);
    }

    for (std::vector<SceneTriangle>& bucket : Buckets) {
        bucket.clear();
        bucket.reserve(5000);
    }

    for (Mesh* mesh : scene) {
        matrix viewWorld = camera * mesh->world;
        matrix projection = renderer.perspective;
        matrix mvp = projection * viewWorld;

        size_t vCount = mesh->vertices.size();
        std::vector<Vertex> tv(vCount);
        std::vector<vec4> vPos(vCount);

        for (size_t i = 0; i < vCount; ++i) {
            vPos[i] = viewWorld * mesh->vertices[i].p;
            tv[i].p = mvp * mesh->vertices[i].p;
            tv[i].p.W();
            tv[i].p[0] = (tv[i].p[0] + 1.f) * 0.5f * (float)renderer.canvas.getWidth();
            tv[i].p[1] = (1.f - (tv[i].p[1] + 1.f) * 0.5f) * (float)renderer.canvas.getHeight();
            tv[i].normal = mesh->world * mesh->vertices[i].normal;
            tv[i].normal.normalise();
            tv[i].rgb = mesh->vertices[i].rgb;
        }

        for (triIndices& ind : mesh->triangles) {
            vec4 e1 = vPos[ind.v[1]] - vPos[ind.v[0]];
            vec4 e2 = vPos[ind.v[2]] - vPos[ind.v[0]];
            vec4 faceNormal = vec4::cross(e1, e2);
            if (vec4::dot(faceNormal, vec4(-vPos[ind.v[0]][0], -vPos[ind.v[0]][1], -vPos[ind.v[0]][2], 0.f)) >= 0.0f) continue;


            SceneTriangle tri = { {tv[ind.v[0]], tv[ind.v[1]], tv[ind.v[2]]}, mesh->ka, mesh->kd };
            float triMinY = std::min({ tri.t[0].p[1], tri.t[1].p[1], tri.t[2].p[1] });
            float triMaxY = std::max({ tri.t[0].p[1], tri.t[1].p[1], tri.t[2].p[1] });

            int firstThread = std::clamp(static_cast<int>(triMinY / Rows), 0, Threads - 1);
            int lastThread = std::clamp(static_cast<int>(triMaxY / Rows), 0, Threads - 1);

            for (int t = firstThread; t <= lastThread; ++t) {
                Buckets[t].push_back(tri);
            }
        }
    }

    if (!initialized) {
        for (int i = 0; i < Threads; i++) {
            pool.emplace_back(threadWork, i, Threads);
        }
        initialized = true;
    }

    {
        std::lock_guard<std::mutex> lock(mtx);
        pRenderer = &renderer;
        pLight = &L;
        finished_count = 0;
        ready = true;
    }
    cv_start.notify_all();

    {
        std::unique_lock<std::mutex> lock(mtx);
        while (finished_count != Threads) {
            cv_done.wait(lock);
        }
        ready = false;
    }
    cv_start.notify_all();
}
// Test scene function to demonstrate rendering with user-controlled transformations
// No input variables
void sceneTest() {
    Renderer renderer;
    Light L{ vec4(0.f, 1.f, 1.f, 0.f), colour(1.0f, 1.0f, 1.0f), colour(0.2f, 0.2f, 0.2f) };
    matrix camera = matrix::makeIdentity();

    std::vector<Mesh*> scene;
    Mesh mesh = Mesh::makeSphere(1.0f, 10, 20);
    scene.push_back(&mesh);

    float x = 0.0f, y = 0.0f, z = -4.0f;
    while (true) {
        FPS();
        renderer.canvas.checkInput();
        renderer.clear();

        mesh.world = matrix::makeTranslation(x, y, z);

        if (renderer.canvas.keyPressed(VK_ESCAPE)) break;
        if (renderer.canvas.keyPressed('A')) x -= 0.1f;
        if (renderer.canvas.keyPressed('D')) x += 0.1f;
        if (renderer.canvas.keyPressed('W')) y += 0.1f;
        if (renderer.canvas.keyPressed('S')) y -= 0.1f;
        if (renderer.canvas.keyPressed('Q')) z += 0.1f;
        if (renderer.canvas.keyPressed('E')) z -= 0.1f;

        render(renderer, scene, camera, L);

        renderer.present();
    }
}

// Utility function to generate a random rotation matrix
// No input variables
matrix makeRandomRotation() {
    RandomNumberGenerator& rng = RandomNumberGenerator::getInstance();
    unsigned int r = rng.getRandomInt(0, 3);

    switch (r) {
    case 0: return matrix::makeRotateX(rng.getRandomFloat(0.f, 2.0f * M_PI));
    case 1: return matrix::makeRotateY(rng.getRandomFloat(0.f, 2.0f * M_PI));
    case 2: return matrix::makeRotateZ(rng.getRandomFloat(0.f, 2.0f * M_PI));
    default: return matrix::makeIdentity();
    }
}

// Function to render a scene with multiple objects and dynamic transformations
// No input variables
void scene1() {
    Renderer renderer;
    matrix camera;
    Light L{ vec4(0.f, 1.f, 1.f, 0.f), colour(1.0f, 1.0f, 1.0f), colour(0.2f, 0.2f, 0.2f) };

    std::vector<Mesh*> scene;
    for (unsigned int i = 0; i < 20; i++) {
        Mesh* m1 = new Mesh(); *m1 = Mesh::makeCube(1.f);
        m1->world = matrix::makeTranslation(-2.0f, 0.0f, (-3 * static_cast<float>(i))) * makeRandomRotation();
        scene.push_back(m1);
        Mesh* m2 = new Mesh(); *m2 = Mesh::makeCube(1.f);
        m2->world = matrix::makeTranslation(2.0f, 0.0f, (-3 * static_cast<float>(i))) * makeRandomRotation();
        scene.push_back(m2);
    }

    float zoffset = 8.0f;
    while (true) {
        FPS();
        renderer.canvas.checkInput();
        renderer.clear();
        camera = matrix::makeTranslation(0, 0, -zoffset);

        scene[0]->world = scene[0]->world * matrix::makeRotateXYZ(0.1f, 0.1f, 0.0f);
        scene[1]->world = scene[1]->world * matrix::makeRotateXYZ(0.0f, 0.1f, 0.2f);

        if (renderer.canvas.keyPressed(VK_ESCAPE)) break;
        zoffset -= 0.1f;

        render(renderer, scene, camera, L);

        renderer.present();
    }
    for (auto& m : scene) delete m;
}

// Scene with a grid of cubes and a moving sphere
// No input variables
void scene2() {
    Renderer renderer;
    matrix camera = matrix::makeIdentity();
    Light L{ vec4(0.f, 1.f, 1.f, 0.f), colour(1.0f, 1.0f, 1.0f), colour(0.2f, 0.2f, 0.2f) };

    std::vector<Mesh*> scene;

    struct rRot { float x; float y; float z; }; // Structure to store random rotation parameters
    std::vector<rRot> rotations;

    RandomNumberGenerator& rng = RandomNumberGenerator::getInstance();

    // Create a grid of cubes with random rotations
    for (unsigned int y = 0; y < 6; y++) {
        for (unsigned int x = 0; x < 8; x++) {
            Mesh* m = new Mesh();
            *m = Mesh::makeCube(1.f);
            scene.push_back(m);
            m->world = matrix::makeTranslation(-7.0f + (static_cast<float>(x) * 2.f), 5.0f - (static_cast<float>(y) * 2.f), -8.f);
            rRot r{ rng.getRandomFloat(-.1f, .1f), rng.getRandomFloat(-.1f, .1f), rng.getRandomFloat(-.1f, .1f) };
            rotations.push_back(r);
        }
    }

    // Create a sphere and add it to the scene
    Mesh* sphere = new Mesh();
    *sphere = Mesh::makeSphere(1.0f, 10, 20);
    scene.push_back(sphere);
    float sphereOffset = -6.f;
    float sphereStep = 0.1f;
    sphere->world = matrix::makeTranslation(sphereOffset, 0.f, -6.f);

    auto start = std::chrono::high_resolution_clock::now();
    std::chrono::time_point<std::chrono::high_resolution_clock> end;
    int cycle = 0;

    bool running = true;
    while (running) {
        FPS();
        renderer.canvas.checkInput();
        renderer.clear();

        // Rotate each cube in the grid
        for (unsigned int i = 0; i < rotations.size(); i++)
            scene[i]->world = scene[i]->world * matrix::makeRotateXYZ(rotations[i].x, rotations[i].y, rotations[i].z);

        // Move the sphere back and forth
        sphereOffset += sphereStep;
        sphere->world = matrix::makeTranslation(sphereOffset, 0.f, -6.f);
        if (sphereOffset > 6.0f || sphereOffset < -6.0f) {
            sphereStep *= -1.f;

        }

        if (renderer.canvas.keyPressed(VK_ESCAPE)) break;

        render(renderer, scene, camera, L);
        renderer.present();
    }

    for (auto& m : scene)
        delete m;
}
void scene3() {
    Renderer renderer;
    matrix camera = matrix::makeIdentity();
    Light L{ vec4(0.f, 1.f, 1.f, 0.f), colour(1.0f, 1.0f, 1.0f), colour(0.2f, 0.2f, 0.2f) };

    std::vector<Mesh*> scene;
    struct rRot { float x, y, z; };
    std::vector<rRot> cubeRotations;
    RandomNumberGenerator& rng = RandomNumberGenerator::getInstance();

    for (unsigned int y = 0; y < 10; y++) {
        for (unsigned int x = 0; x < 5; x++) {
            Mesh* m = new Mesh();
            *m = Mesh::makeSphere(0.4f, 10, 20);
            m->world = matrix::makeTranslation(2.0f + (x * 1.0f), 4.5f - (y * 1.0f), -10.f);
            scene.push_back(m);
            cubeRotations.push_back({ rng.getRandomFloat(-.02f, .02f), rng.getRandomFloat(-.02f, .02f), rng.getRandomFloat(-.02f, .02f) });
        }
    }

    Mesh* aSphere = new Mesh();
    *aSphere = Mesh::makeSphere(1.5f, 40, 80);

    scene.push_back(aSphere);

    float sphereY = 0.0f;
    float sphereStep = 0.06f;

    bool running = true;
    while (running) {
        FPS();
        renderer.canvas.checkInput();
        renderer.clear();

        for (size_t i = 0; i < cubeRotations.size(); i++) {
            scene[i]->world = scene[i]->world * matrix::makeRotateXYZ(cubeRotations[i].x, cubeRotations[i].y, cubeRotations[i].z);
        }

        sphereY += sphereStep;
        aSphere->world = matrix::makeTranslation(-4.0f, sphereY, -8.f);
        if (sphereY > 4.5f || sphereY < -4.5f) sphereStep *= -1.f;

        if (renderer.canvas.keyPressed(VK_ESCAPE)) break;

        render(renderer, scene, camera, L);

        renderer.present();
    }

    for (auto& m : scene) delete m;
}

// Entry point of the application
// No input variables
int main() {
    // Uncomment the desired scene function to run
    //scene1();
    scene2();
    //scene3();
    //sceneTest(); 


    return 0;
}