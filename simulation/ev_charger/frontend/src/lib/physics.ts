type Physics = {_sim_estimate_seconds:(capacity:number,from:number,to:number,watts:number)=>number};
export async function loadPhysics():Promise<Physics> {
 const url='/wasm/physics.js';
 const {default:factory}=await import(/* @vite-ignore */ url);
 return factory({locateFile:(file:string)=>'/wasm/'+file});
}
