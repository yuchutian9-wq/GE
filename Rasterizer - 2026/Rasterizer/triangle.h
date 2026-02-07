#pragma once

#include "mesh.h"
#include "colour.h"
#include "renderer.h"
#include "light.h"
#include <iostream>
#include <algorithm>
#include <cmath>

// Simple support class for a 2D vector
class vec2D {
public:
    float x, y;

    // Default constructor initializes both components to 0
    vec2D() { x = y = 0.f; };

    // Constructor initializes components with given values
    vec2D(float _x, float _y) : x(_x), y(_y) {}

    // Constructor initializes components from a vec4
    vec2D(vec4 v) {
        x = v[0];
        y = v[1];
    }

    // Display the vector components
    void display() { std::cout << x << '\t' << y << std::endl; }

    // Overloaded subtraction operator for vector subtraction
    vec2D operator- (vec2D& v) {
        vec2D q;
        q.x = x - v.x;
        q.y = y - v.y;
        return q;
    }
};

// Class representing a triangle for rendering purposes
class triangle {
    Vertex v[3];       // Vertices of the triangle
    float area;        // Area of the triangle
    colour col[3];     // Colors for each vertex of the triangle

public:
    // Constructor initializes the triangle with three vertices
    // Input Variables:
    // - v1, v2, v3: Vertices defining the triangle
    triangle(const Vertex& v1, const Vertex& v2, const Vertex& v3) {
        v[0] = v1;
        v[1] = v2;
        v[2] = v3;

        // Calculate the 2D area of the triangle
        vec2D e1 = vec2D(v[1].p - v[0].p);
        vec2D e2 = vec2D(v[2].p - v[0].p);
        area = std::fabs(e1.x * e2.y - e1.y * e2.x);
    }

    // Helper function to compute the cross product for barycentric coordinates
    // Input Variables:
    // - v1, v2: Edges defining the vector
    // - p: Point for which coordinates are being calculated
    float getC(vec2D v1, vec2D v2, vec2D p) {
        vec2D e = v2 - v1;
        vec2D q = p - v1;
        return q.y * e.x - q.x * e.y;
    }

    // Compute barycentric coordinates for a given point
    // Input Variables:
    // - p: Point to check within the triangle
    // Output Variables:
    // - alpha, beta, gamma: Barycentric coordinates of the point
    // Returns true if the point is inside the triangle, false otherwise
    bool getCoordinates(vec2D p, float& alpha, float& beta, float& gamma) {
        alpha = getC(vec2D(v[0].p), vec2D(v[1].p), p) / area;
        beta = getC(vec2D(v[1].p), vec2D(v[2].p), p) / area;
        gamma = getC(vec2D(v[2].p), vec2D(v[0].p), p) / area;

        if (alpha < 0.f || beta < 0.f || gamma < 0.f) return false;
        return true;
    }

    // Template function to interpolate values using barycentric coordinates
    // Input Variables:
    // - alpha, beta, gamma: Barycentric coordinates
    // - a1, a2, a3: Values to interpolate
    // Returns the interpolated value
    template <typename T>
    T interpolate(float alpha, float beta, float gamma, T a1, T a2, T a3) {
        return (a1 * alpha) + (a2 * beta) + (a3 * gamma);
    }

    // Draw the triangle on the canvas
    // Input Variables:
    // - renderer: Renderer object for drawing
    // - L: Light object for shading calculations
    // - ka, kd: Ambient and diffuse lighting coefficients
    void draw(Renderer& renderer, Light& L, float ka, float kd, int minY, int maxY) {
        if (area < 1.f) return;
        vec2D minV, maxV;
        getBoundsWindow(renderer.canvas, minV, maxV);

        int startY = std::max((int)std::floor(minV.y), minY);
        int endY = std::min((int)std::ceil(maxV.y), maxY);
        int startX = (int)std::floor(minV.x);
        int endX = (int)std::ceil(maxV.x);

        float invArea = 1.0f / area;

        float da_dx = (v[0].p[1] - v[1].p[1]) * invArea;
        float da_dy = (v[1].p[0] - v[0].p[0]) * invArea;

        float db_dx = (v[1].p[1] - v[2].p[1]) * invArea;
        float db_dy = (v[2].p[0] - v[1].p[0]) * invArea;

        float dg_dx = (v[2].p[1] - v[0].p[1]) * invArea;
        float dg_dy = (v[0].p[0] - v[2].p[0]) * invArea;

        float dDepth_dx = db_dx * v[0].p[2] + dg_dx * v[1].p[2] + da_dx * v[2].p[2];
        float dR_dx = db_dx * v[0].rgb[colour::RED] + dg_dx * v[1].rgb[colour::RED] + da_dx * v[2].rgb[colour::RED];
        float dG_dx = db_dx * v[0].rgb[colour::GREEN] + dg_dx * v[1].rgb[colour::GREEN] + da_dx * v[2].rgb[colour::GREEN];
        float dB_dx = db_dx * v[0].rgb[colour::BLUE] + dg_dx * v[1].rgb[colour::BLUE] + da_dx * v[2].rgb[colour::BLUE];

        float a_row, b_row, g_row;
        getCoordinates(vec2D((float)startX, (float)startY), a_row, b_row, g_row);

        for (int y = startY; y < endY; y++) {
            float alpha = a_row;
            float beta = b_row;
            float gamma = g_row;

            float depth = beta * v[0].p[2] + gamma * v[1].p[2] + alpha * v[2].p[2];
            float r = beta * v[0].rgb[colour::RED] + gamma * v[1].rgb[colour::RED] + alpha * v[2].rgb[colour::RED];
            float g = beta * v[0].rgb[colour::GREEN] + gamma * v[1].rgb[colour::GREEN] + alpha * v[2].rgb[colour::GREEN];
            float b = beta * v[0].rgb[colour::BLUE] + gamma * v[1].rgb[colour::BLUE] + alpha * v[2].rgb[colour::BLUE];

            for (int x = startX; x < endX; x++) {
                if (alpha >= 0.f && beta >= 0.f && gamma >= 0.f) {
                    if (renderer.zbuffer(x, y) > depth && depth > 0.001f) {
                        vec4 normal = (v[0].normal * beta) + (v[1].normal * gamma) + (v[2].normal * alpha);
                        normal.normalise();

                        float dot = std::max(vec4::dot(L.omega_i, normal), 0.0f);

                        unsigned char cr = static_cast<unsigned char>(std::min((r * kd * dot + L.ambient[colour::RED] * ka), 1.0f) * 255);
                        unsigned char cg = static_cast<unsigned char>(std::min((g * kd * dot + L.ambient[colour::GREEN] * ka), 1.0f) * 255);
                        unsigned char cb = static_cast<unsigned char>(std::min((b * kd * dot + L.ambient[colour::BLUE] * ka), 1.0f) * 255);

                        renderer.canvas.draw(x, y, cr, cg, cb);
                        renderer.zbuffer(x, y) = depth;
                    }
                }
                alpha += da_dx; beta += db_dx; gamma += dg_dx;
                depth += dDepth_dx; r += dR_dx; g += dG_dx; b += dB_dx;
            }
            a_row += da_dy; b_row += db_dy; g_row += dg_dy;
        }
    }

    // Compute the 2D bounds of the triangle
    // Output Variables:
    // - minV, maxV: Minimum and maximum bounds in 2D space
    void getBounds(vec2D& minV, vec2D& maxV) {
        minV = vec2D(v[0].p);
        maxV = vec2D(v[0].p);
        for (unsigned int i = 1; i < 3; i++) {
            minV.x = std::min(minV.x, v[i].p[0]);
            minV.y = std::min(minV.y, v[i].p[1]);
            maxV.x = std::max(maxV.x, v[i].p[0]);
            maxV.y = std::max(maxV.y, v[i].p[1]);
        }
    }

    // Compute the 2D bounds of the triangle, clipped to the canvas
    // Input Variables:
    // - canvas: Reference to the rendering canvas
    // Output Variables:
    // - minV, maxV: Clipped minimum and maximum bounds
    void getBoundsWindow(GamesEngineeringBase::Window& canvas, vec2D& minV, vec2D& maxV) {
        getBounds(minV, maxV);
        minV.x = std::max(minV.x, static_cast<float>(0));
        minV.y = std::max(minV.y, static_cast<float>(0));
        maxV.x = std::min(maxV.x, static_cast<float>(canvas.getWidth()));
        maxV.y = std::min(maxV.y, static_cast<float>(canvas.getHeight()));
    }

    // Debugging utility to display the triangle bounds on the canvas
    // Input Variables:
    // - canvas: Reference to the rendering canvas
    void drawBounds(GamesEngineeringBase::Window& canvas) {
        vec2D minV, maxV;
        getBounds(minV, maxV);

        for (int y = (int)minV.y; y < (int)maxV.y; y++) {
            for (int x = (int)minV.x; x < (int)maxV.x; x++) {
                canvas.draw(x, y, 255, 0, 0);
            }
        }
    }

    // Debugging utility to display the coordinates of the triangle vertices
    void display() {
        for (unsigned int i = 0; i < 3; i++) {
            v[i].p.display();
        }
        std::cout << std::endl;
    }
};
