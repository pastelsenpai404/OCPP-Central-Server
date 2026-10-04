export type Charger = {id:string;protocol:string;mode:string;status:string;fault:string;plugged:boolean;online:boolean;registered:boolean;network:boolean;available:boolean;uncertain:boolean;transaction:string;soc:number;targetSoc:number;capacityKwh:number;powerKw:number;energyKwh:number;gridKw:number;maxKw:number;temperature:number;speed:number;paused:boolean;elapsedSeconds:number;queued:number};
export type Event = {time:string;station:string;type:string;message:unknown;sequence:number};
export async function api<T>(token:string,path:string,body?:unknown):Promise<T> {
 const response=await fetch('/api/v1/'+path,{method:body===undefined?'GET':'POST',headers:{Authorization:'Bearer '+token,...(body===undefined?{}:{'Content-Type':'application/json'})},body:body===undefined?undefined:JSON.stringify(body)});
 if(!response.ok) throw new Error(response.status===401?'Token ไม่ถูกต้อง':response.status===400?'คำสั่งไม่ผ่าน: ตรวจสถานะตู้และค่าที่กรอก':'เชื่อมต่อไม่ได้ ('+response.status+')');
 return response.json();
}
