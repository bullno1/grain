Grain, a domain specific language for particle systems
======================================================

## Introduction
### Overview

This project aims to implement "Grain", a domain-specific language (DSL) for creating particle systems. It consists of two parts:

* grainc: a compiler which compiles Grain code to GLSL (OpenGL shading language) to be executed on the GPU
* grainr: a C++ runtime library for using the generated code in an OpenGL program

### Introduction to particle systems

Particle system is a rendering technique which uses a large number of many small sprites called 'particle'.
Each particle can have several attributes such as: life, velocity, colour, acceleration, mass etc...
Some common particle effects are fire, smoke, rain...

In the beginning of its life cycle, a particle comes out of an emitter.
An emitter serves as an initializer for a particle.
Usually, it randomly assigns a particle’s attributes to a predefined range.
An emitter also defines how fast or slow particles are generated.

A particle's attributes can change overtime under the effect of affectors.
Some examples of affectors are: attractor which pulls particles towards it (changing velocity over time), fader makes particles change brightness and gradually fade away.

A particle can also be destroyed by destructors.
For example, when a rain drop reaches the ground, it is removed from the system by a plane destructor.

### Motivations

Many existing particle systems have a predefined set of primitives (emitters, affectors, particle attributes...).
Users would use a GUI tool to tweak parameters to create new effects.
The more available primitives there are the more complex the tools become.
At the same time, extending existing systems is difficult.

Grain was created as an alternative way to create particle systems.
As a language, it allows users to create arbitrary effects.
It also promotes modularity and code reuse.
Grain is not intended to replace GUI editors.
To do fine tuning of parameters, a visual editor is still recommended.
However, the behaviour of a particle system can be defined using a DSL.

## Language
### Syntax

Grain's syntax is derived from GLSL which in turn, is derived from C.
The overall structure of a Grain script is as follow:

```glsl
// Start with a declaration block
@require linear_motion
@param vec2 center
@param float radius

// Follow by a body block
vec2 normal = position - center;
bool inCircle = length(normal) < radius;
bool goingIn = dot(normal, velocity) < 0.0;
bool bounce = inCircle && goingIn;
vec2 newV = reflect(velocity, normalize(normal));
velocity = (bounce) ? newV / 3 : velocity;
life = select(bounce, life / 2, life);
```

A declaration always starts with `@`.
Available declarations are:

* `@require <script>`: This script requires and depends on another script
* `@attribute type name`: This script requires that all particles must have the specified attribute with the given type.
  Available types are: float, vec2, vec3, vec4.
* `@param type name`: Declare a script parameter with the given type.
* `@declare decl`: Arbitrary declaration.
  Whatever appears after @declare will be copied verbatim to the beginning of the generated GLSL.
  This is to allow programmers to use GLSL-specific features such as graphic card’s capability detection.

The body block follows GLSL’s normal syntax.
What it should do depends on the type of script.

### Types of scripts

#### Emitter
An emitter must initialize a particle’s attributes.
It is usually done with some randomization.

Emitters can "require" each other.
When emitter A requires emitter B, all particle attributes declared in emitter B will also be available in emitter A.
Moreover, emitter B's initialization code will be called before emitter A's.
Emitter B, in turn, can also require other emitters.

#### Affector
A affector alters a particle's attributes.
It is usually done with respect to time.

A affector can also have affector-local parameters (declared with `@param`).
Affectors can “require” each other.
Similar to emitter, all particle attributes and script parameters from the required script will be available and the required code will also be invoked before the requiring code.

In Grain, destructors are affectors which set the built-in attribute `life` to 0.

#### Renderer
A render script takes a particle system's attributes and transforms them into OpenGL-specified format.
It consists of two parts: vertex shader and fragment shader.

The vertex shader must write to `gl_Position`.
The fragment shader must output a colour to at least one render buffer.
To ensure that only live particles are visible, the `w` component of `gl_Position` must be 1.0 if the particle is alive (`life > 0`) and must be 0.0 if it is dead (`life <= 0`).

As compositing render scripts is quite complex, @require statements are ignored, a render script must be written in its entirety instead of being created from smaller modules.

### Types of variables

As mentioned before, there are two types of variables: script parameters and particle attributes. They both live in the same namespace.
Their differences are as follow:

|                          |Script parameter                 |Particle attribute          |
|--------------------------|---------------------------------|----------------------------|
|Accessibility in a script |read-only                        |read-write                  |
|Accessibility in C++      |write-only                       |not accessible              |
|Values in particles       |same for all particles           |different for each particle |
|Examples                  |gravity constant, wind direction |position, velocity, mass    |

Name clashing can happen in the common namespace.
For example, a particle can have an attribute called `position`.
The particle system has a script models a fan to blow particles away.
The position of the fan can be aptly named `position` too.
This system will fail to compile.
To mitigate this problem, a certain naming convention is needed.
For example, script parameters can be prefixed with `g_` for global as their values are the same for all particles.

### Semantics

A particle system is defined by a set of scripts that specifies its behaviour.
The set of attributes a system has is implicitly defined as the union of all required attributes in all scripts.
This creates great flexibility.
For example, a particle system that does not require non-uniform acceleration should not contain the mass attribute and waste memory.
However, if the user changes his mind and includes an affector which applies force on every particle, the system will automatically create the mass attribute so that the affector can calculate acceleration from force.

A Grain particle system must be controlled from a host program since Grain is only a DSL.
Each time the host program calls a script, multiple instances of it will be launched concurrently.
Each instance will work on a specific particle.
Interaction between particles (N-body) is not yet possible.
It is similar to a map operation in functional languages.
In fact, as it will be shown later, even emitter scripts which seem to be creating new particles are also just doing map operations.

### Built-in functions and variables

Besides what GLSL provides, Grain also introduces other built-in functions and variables:

* `float random_range(float lower, float upper)`: returns a different number each time it is called.
* `genType select(bool condition, genType ifTrue, genType ifFalse)`: similar to C’s ternary operator, returns ifTrue if condition is true and ifFalse if condition is false.
  Under the hood, it uses GLSL’s mix function to avoid branching.
* `float dt`: time passed since last frame.
  Usually used for animation.
* `float life`: when it reaches 0.0, the particle is considered dead.
  `life` is not automatically decremented.
  `aging.affector` script takes care of that.

## Implementation details
### Persistent state

Particle states are stored in floating point textures.
They are similar to regular texture in the sense that they are both 2D arrays of pixels which can be sampled using coordinates.
Each pixel is a 4D vector with four components: red, green, blue and alpha.
A regular texture has its elements clamped to [0, 1] but a floating point textures have each element being a full 32-bit floating point number so it can be used to store general purpose data.

An appropriate number of textures are automatically allocated based on the number of attributes.
For example, with life (scalar), position (2D vector), velocity (2D vector), 5 floating point numbers are needed for each particle.
Thus, attributes are stored on 2 textures (capable of storing up to 8 floating point numbers per particle).
However, OpenGL forbids reading and writing to the same texture at the same time so the number of textures is doubled.
The two set of textures are used alternately as input and output.
This is known as ping-pong technique in GPGPU

### Shader generation

The grain compiler works by transforming each script into a function with all parameters passed-by-reference.
For example the following piece of code:

```glsl
@require linear_motion
@param vec2 gravity

velocity += gravity*dt;
```

Will be transformed into:

```glsl
uniform vec2 gravity;

void uniform_gravity(inout float life, inout vec2 velocity, inout vec2 position)
{
velocity += gravity*dt;
}
```

In order to have a complete shader, it needs a `main` function:

```glsl
void main()
{
// Fetch attributes from textures to local variables.
// float life = fetch_life();
// vec2 velocity = fetch_velocity();
// ...

// Update/initialize particles
// Functions are called in order of dependency to update attributes in-place
linear_motion(life, velocity, position);
uniform_gravity(life, velocity, position);

// Store new attributes into textures
// store_life(life);
// store_velocity(velocity);
// ...
}
```

The fetch part is straightforward.
Each particle corresponds to a coordinate (e.g.: first particle is (0, 0), second particle is (1, 0)...).
The textures are sampled at those coordinates to get the particle's attributes.
Components of the 4D vectors are assigned to appropriate local variables.

For the update/initialization part, wrapping code in a function instead of pasting it directly into the main function has several advantages.
First, it gives each script its own scope for local variables.
Second, parameters can be easily renamed without having to do any analysis on the code.
For example, by calling `linear_motion` as `linear_motion(_life, velocity, position)`, its modification to the life attribute will be written to the temporary variable `_life` and be discarded instead.
This is important for the implementation of emitter script.

The store part is just a reverse process of fetch. Attributes are grouped into 4D vectors again and written to target textures.

### Particle generation

Since render scripts do not render dead (`life <= 0`) particles, merely set the status of a dead particle to "alive" (`life > 0`) and move it back to the emission point creates the illusion of a new particle being created.

An emitter script is very similar to a affector script.
It is invoked on every single particle every frame.
However, it does not immediately store the newly calculated attributes to the output textures.
Two criteria need to be met:

* The current particle must be "dead" (`life <= 0.0`)
* A random number is less than a certain threshold

The random event is important to control rate of emission.
In CPU-based systems, a mutable counter usually is used to keep track of how many particles have been emitted so far in a frame and thus, an exact number of particles are always emitted every cycle.

However, in a GPU-based current system, there is no shared mutable state.
There is no way to keep a counter, thus, a probabilistic method must be used.
The threshold, along with the rate of particle destruction determines how thick or thin the particle system is.
For example, if a system can only have 2048 particles at the same time, and it is currently having no particle, by setting the threshold to 0.5, roughly 1024 particles will be emitted in the current frame.
Next frame, about 512 particles will be emitted.
Since particles are also destroyed, the system will eventually reach equilibrium.

The generated code is as follow:

* Particle attributes are first fetched into local variables as usual.
* The initialize function is called on a different set of variables to avoid overwriting old states.
* One of the two sets of variables will be chosen randomly as output using the method mentioned above.

### Random number generation

GLSL does not provide a random function so a noise function is used instead.
Some variables are used as input to this noise function to provide pseudo-random numbers:

* Particle id
* Current system's time
* An incrementing counter

Particle id ensures that each particle gets a different stream of random numbers.
System time gives each frame a different set of random number streams.
Finally, the counter is used to sample this stream at different points every time the random function is called.
With particle id and system time being uniforms or GLSL's immutable script-scoped variables, the signature for `random_range` would be:

```glsl
float random_range(in int counter, in float lower, in float upper);
```

Relying on users to pass the counter around and incrementing it after every invocation is troublesome and error-prone.
The pre-processor is used to hide this inconvenience:

```glsl
#define random_range(lower, upper) random_range_(counter, lower, upper)

float random_range(inout int counter, in float lower, in float upper)
{
    ++counter;
    return lower + noise(counter, time, particleId) * (upper – lower);
}
```

Thus, every call to `random_range(lower, upper)` is translated into `random_range_(counter, lower, upper)` instead.
The counter variable is first initialized to 0 in the main function and then threaded along every function to ensure that within a shader’s invocation, every call to `random_range_` receives a different counter value.
Thus, a generated function would actually have a signature like this:

```glsl
void update_particle(inout int counter, inout life, .../*other attributes*/);
```

The main function would look more like this:

```glsl
void main()
{
int counter = 0;
//Fetch

//Update particles
//Functions are called in order of dependency
linear_motion(counter, life, velocity, position);//counter might be modified
gravity(counter, life, velocity, position);//its new value is passed here

//Store
}
```

### Optimizations

Certain graphic cards and drivers perform poorly when given shaders with a deep call stack.

The open source library `glsl-optimizer` is used to optimize the generated code.
It performs several techniques including dead variable removal, inlining, reusing intermediate calculations.
This ensures that end-user can focus on creating particle systems by mixing and matching scripts without sacrificing too much performance.

## Discussion and future works

* The current random generator does not uniformly distribute its output.
  As a result, there are noticeable repeating emission patterns in supposedly random particle systems.
  A better generator is needed.
* Due to source code generation, when a syntax error is made, the wrong line number will be reported by the GLSL compiler.
  `#line` pre-processor directive can be used to remap lines to correct ones.
* Some steps in the code generation phase can be abstracted out so that alternative strategies can be implemented.
  For example, a deterministic approach to particle management can be used instead of the current probabilistic one.

## Appendix

### Related works
* "Building a million-particle system" [^1] serves as the initial inspiration and implementation outline for this project.
  It shows how shaders which are traditionally thought to be stateless can be used to create a system with persistent state.
  It suggests a CPU-based particle lifetime management method.
* "A thoroughly modern particle system" [^2] talks about a probabilistic approach to manage particles.
  It argues that the method is faster and more suitable for real-time applications.
  With many particles, users would not notice the random emission rate.
* "A Declarative API for Particle Systems" [^3] describes a DSL with a similar purpose.
  The DSL is uses ML-like syntax and it can be compiled to run on both CPU and GPU.
  It features a combinatory system to combine particle behaviours in a type-safe manner.
  The combinatory system is very similar to Grain's `@require`.
  For particle lifetime management on GPU, it uses a parallel-scan algorithm to count the exact number of live particles.

### Examples

`geyser` is a program which simulates a water fountain effect.
It demonstrates some basic scripts:

Particles are emitted from a point_emitter:

```glsl
@param vec2 emission_point
@param float min_speed
@param float max_speed
@param float min_angle
@param float max_angle
@attribute vec2 position
@attribute vec2 velocity
@require aging

position = emission_point;
float speed = random_range(min_speed, max_speed);
float angle = random_range(min_angle, max_angle);
velocity = vec2(cos(angle), sin(angle)) * speed;
```

`linear_motion.affector` moves a particle by its velocity:

```glsl
@attribute vec2 velocity
@attribute vec2 position

position += velocity*dt;
```

`uniform_gravity.affector` modifies a particle’s velocity every frame:

```glsl
@param vec2 gravity
@require linear_motion

velocity += gravity * dt;
```

`point.vsh` and `point.fsh` renders every particle as a point on the screen:

```glsl
//point.vsh
@declare uniform mat4x4 uMVP; //model-view-projection matrix

float alive = float(life > 0.0);
gl_Position = uMVP * vec4(position.x, position.y, 0.0, alive);
```

```glsl
//point.fsh
@declare out vec4 out0;

out0 = vec4(0.0, 1.0, 1.0, 1.0); //use a fixed color for everything
```

`rain` is a more complex program to demonstrate that scripts can be mix-and-matched and a system can be under the influence of multiple affectors.
Beside one `uniform_gravity.affector` to simulate rain drops falling, there are also two `circle_deflector`s to deflect rain drops with one being attached to the mouse to show the interactivity of the system.
To simulate rain, particles are emitted from a `line_emitter` instead of a `point_emitter`.

```glsl
//line_emitter.emitter
@param float width
@param float height
@param float max_horizontal_speed
@attribute vec2 position
@attribute vec2 velocity
@require aging

position = vec2(random_range(-0.5, 0.5) * width, height);
velocity = vec2(random_range(-0.5, 0.5) * max_horizontal_speed, 0.0);
```

```glsl
//circle_deflector.affector
@require linear_motion
@param vec2 center
@param float radius

vec2 normal = position - center;
bool inCircle = length(normal) < radius;
bool goingIn = dot(normal, velocity) < 0.0;
bool bounce = inCircle && goingIn;
vec2 newV = reflect(velocity, normalize(normal));
velocity = (bounce) ? newV / 3 : velocity;
life = select(bounce, life / 2, life);
```

### User manual
#### grainc

`grainc` is a compiler to compile Grain to GLSL.
Usage:

> grainc [options] files...
>
> Valid options are:
>
> -o <output>         Set output filename (default: a.out)
> -O                  Enable optimization
>
> Examples:
> grainc –O –o rain line.emitter circle_deflector.affector

#### grainr

`grainr` is an API library to use a compiled script in an application.
It has the following classes:

* `Context`: Each program needs to have at least one instance.
  It holds the environment for the runtime library. It can create instances of `SystemDefinition`.
* `SystemDefinition`: holds the data for particle systems of the same type.
  It can create instances of `ParticleSystem`.
* `ParticleSystem`: holds the state of a particle system.
  Its state can be modified and rendered by different types of script: `Affector`, `Emitter`, and `Renderer`.

#### How to get and compile code

The source code can be found on github: https://github.com/bullno1/grain
Currently, the project is only tested on Linux.
The following packages are required:

* OpenGL
* GLEW
* SDL (only needed to compile examples)
* glm (only needed to compile examples)
* cmake
* ninja

The shell scripts at the root of the project will automatically build and run the appropriate demo/tool.

#### References

[^1]: L. Latta, "Building a Million-Particle System" 28 July 2004. [Online].
      Available: http://www.gamasutra.com/view/feature/130535/building_a_millionparticle_system.php.

[^2]: "A Thoroughly Modern Particle System" 6, October 2009. [Online].
      Available: http://directtovideo.wordpress.com/2009/10/06/a-thoroughly-modern-particle-system/.

[^3]: P. Krajcevski and J. Reppy, "A Declarative API for Particle Systems" Disney Interactive Studios and University of Chicago, 2011.
