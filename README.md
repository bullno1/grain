# grain

Grain is a GPU-driven composable particle system with its own DSL.

It is designed to be used with [Cute Framework](https://github.com/RandyGaul/cute_framework).

# Building

`./bootstrap` to pull all dependencies.

## Linux

```
cmd/linux/build
```

## Windows

```
cmd/win/prepare.bat
cmd/win/build.bat
```

## Web

```
cmd/web/build
```

# Concepts

To procedurally define a particle system, we need to create several modules.
A module is a self-contained compilation unit with:

* A `Module` block to declare its name.
  A module name has to be unique within its kind (explained below).
* A `Requires` block listing which particle attributes it needs.
  An "attribute" is what describes an individual particle.
  For example: position, velocity...
* A `Params` block listing the kind of parameters that can be used to tweak the module.
  A "parameter" is what describes a module, not a particle.
  For example, in a `Gravity` module, the gravity constant is a module parameter.
* A GLSL function with the signature `void process(inout ParticleAttrs particle, ModuleParams params, Ctx ctx)`.
  It will be run on the GPU for each particle.

  * `ParticleAttrs` is a struct containing all the fields in the `Requires` block.
  * `ModuleParams` is a struct containing all the fields in the `Params` block.
  * `Ctx` is a system defined struct with timing informations such as delta time or elapsed time.

Modules are classified into several kinds:

* Emitter: Initializes a particle's attributes.

  The `process` function will be called on a newly created particle.
  A particle system can have multiple emitters, each will initializes its different aspects.
  For example: a point emitter will set the initial position and velociy while an age emitter sets the initial lifetime.
* Affector: Modifies a particle.

  The `process` function will be called on each particle.
  The function can do anything to a particle's attributes.

  A particle system can have any number of affectors.
* Renderer: Render a particle.

  It consists of two parts: a vertex shader and a fragment shader.
  It works like any graphic shader: The vertex stage has to write to `gl_Position` and various varyings for the fragment shader.
  The fragment shader decides the color of each fragment in a particle.
  However, instead of taking input from a geometry buffer, the input is a particle's various attributes.
  Just like emitters and affector, its entry point is not `main`, but `process` with the above signature.

  A particle system can only have a single renderer.

By combining different emitters, affectors and renderers, complex effects can be created.
The library will automatically compose a particle type (`ParticleAttrs`) that contains all the required attributes across all modules.

As an optimization, a specific combination of modules is called an archetype.
Particle systems sharing the same archetype are organized into a pool with all resources preallocated.
Spawning and destroying a particle system from a pool is fairly cheap.
Moreover, updating and rendering of particle systems belonging to the same archetype will be batched into a single draw call.

To help with authoring, the library also supports live reload of module code.
Changing the code within a `process` function of a module should have an immediate effect on all affected particle system.
Changing the members in a `Params` block will trigger a migration: all parameters with the same name and type will be copied over to the new module.
Only structural change of a particle's attributes will result in a reset of the particle system:

* Adding or removing members from a `Requires` block such that it changes the composition of a `ParticleAttrs` type.
* Adding or removing modules from an archetype that results in structural change to `ParticleAttrs`.
